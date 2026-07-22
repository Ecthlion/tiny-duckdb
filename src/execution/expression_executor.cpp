#include "tiny_duckdb/execution/expression_executor.hpp"

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 3 - TASK #1: the ExpressionExecutor
//!
//! Everything goes through Vector::GetValue/SetValue here. This is slower
//! than DuckDB's specialized tight loops over raw arrays, but much easier to
//! read. Specializing hot paths per type is left as an exercise (docs/lab3.md).
//!
//! ----------------------------------------------------------------------------
//! Task L3.T1a - ExpressionExecutor::Evaluate
//!
//! Evaluate a bound expression tree against an input DataChunk and write the
//! results (one value per input row) into `result`. Handle these expression
//! types (expr.type):
//!
//!   COLUMN_REF       copy input column BoundColumnRefExpression::column_index
//!                    of `chunk` into `result`, row by row
//!   VALUE_CONSTANT   broadcast BoundConstantExpression::value to every row
//!   COMPARE_*        recursively evaluate left/right into two scratch
//!                    Vectors, then compare row-wise; if EITHER side is NULL
//!                    the result is NULL (Value::Null(LogicalType::Boolean()))
//!   CONJUNCTION_AND/OR
//!                    recursively evaluate left/right as booleans; simplified
//!                    three-valued logic: a NULL input counts as false, the
//!                    result itself is never NULL
//!   OPERATOR_ADD/SUBTRACT/MULTIPLY/DIVIDE
//!                    recursive evaluation, then combine row-wise with
//!                    Value::Add / Value::Subtract / Value::Multiply /
//!                    Value::Divide (these already propagate NULL)
//!
//! Hint: the comparison dispatch is already written for you below
//!       (EvaluateComparison) - call it with expr.type and the two Values.
//! Hint: Vector's constructor takes the LogicalType of the values it holds;
//!       allocate scratch vectors with the child's return_type.
//! Hint: useful Value methods: IsNull, GetBoolean, Equals, LessThan.
//!
//! Tests: Lab3ExecutionTest.ExpressionEvaluator* (lab3_execution_test.cpp)
//!
//! ----------------------------------------------------------------------------
//! Task L3.T1b - ExpressionExecutor::Select
//!
//! Evaluate a boolean expression and record the indexes of the rows where it
//! is TRUE into `sel` (a SelectionVector); return the number of matches.
//! NULL results do NOT match. This is the workhorse behind the WHERE clause
//! and the zone-map-less filtering in PhysicalFilter.
//!
//! Hint: evaluate the expression into a boolean Vector first, then walk it.
//!
//! Tests: Lab3ExecutionTest.ExpressionEvaluatorSelect*
//! ============================================================================

static bool EvaluateComparison(ExpressionType comparison, const Value &left, const Value &right) {
	bool eq = Value::Equals(left, right);
	switch (comparison) {
	case ExpressionType::COMPARE_EQUAL:
		return eq;
	case ExpressionType::COMPARE_NOT_EQUAL:
		return !eq;
	case ExpressionType::COMPARE_LESS_THAN:
		return Value::LessThan(left, right);
	case ExpressionType::COMPARE_LESS_THAN_OR_EQUAL:
		return eq || Value::LessThan(left, right);
	case ExpressionType::COMPARE_GREATER_THAN:
		return !eq && !Value::LessThan(left, right);
	case ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL:
		return eq || !Value::LessThan(left, right);
	default:
		throw ExecutorException("not a comparison");
	}
}

void ExpressionExecutor::Evaluate(const BoundExpression &expr, DataChunk &chunk, Vector &result) {
	// [SOLUTION BEGIN L3.T1]
	switch (expr.type) {
	case ExpressionType::COLUMN_REF: {
		auto &col_expr = expr.Cast<BoundColumnRefExpression>();
		auto &input = chunk.GetVector(col_expr.column_index);
		for (idx_t i = 0; i < chunk.size(); i++) {
			result.SetValue(i, input.GetValue(i));
		}
		return;
	}
	case ExpressionType::VALUE_CONSTANT: {
		auto &const_expr = expr.Cast<BoundConstantExpression>();
		for (idx_t i = 0; i < chunk.size(); i++) {
			result.SetValue(i, const_expr.value);
		}
		return;
	}
	case ExpressionType::COMPARE_EQUAL:
	case ExpressionType::COMPARE_NOT_EQUAL:
	case ExpressionType::COMPARE_LESS_THAN:
	case ExpressionType::COMPARE_LESS_THAN_OR_EQUAL:
	case ExpressionType::COMPARE_GREATER_THAN:
	case ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL: {
		auto &cmp = expr.Cast<BoundComparisonExpression>();
		Vector left(cmp.left->return_type);
		Vector right(cmp.right->return_type);
		Evaluate(*cmp.left, chunk, left);
		Evaluate(*cmp.right, chunk, right);
		for (idx_t i = 0; i < chunk.size(); i++) {
			auto lval = left.GetValue(i);
			auto rval = right.GetValue(i);
			if (lval.IsNull() || rval.IsNull()) {
				// comparison with NULL is NULL
				result.SetValue(i, Value::Null(LogicalType::Boolean()));
			} else {
				result.SetValue(i, Value::Boolean(EvaluateComparison(expr.type, lval, rval)));
			}
		}
		return;
	}
	case ExpressionType::CONJUNCTION_AND:
	case ExpressionType::CONJUNCTION_OR: {
		auto &conj = expr.Cast<BoundConjunctionExpression>();
		Vector left(LogicalType::Boolean());
		Vector right(LogicalType::Boolean());
		Evaluate(*conj.left, chunk, left);
		Evaluate(*conj.right, chunk, right);
		for (idx_t i = 0; i < chunk.size(); i++) {
			// simplified three-valued logic: NULL counts as false
			auto lval = left.GetValue(i);
			auto rval = right.GetValue(i);
			bool lbool = !lval.IsNull() && lval.GetBoolean();
			bool rbool = !rval.IsNull() && rval.GetBoolean();
			bool out = expr.type == ExpressionType::CONJUNCTION_AND ? (lbool && rbool) : (lbool || rbool);
			result.SetValue(i, Value::Boolean(out));
		}
		return;
	}
	case ExpressionType::OPERATOR_ADD:
	case ExpressionType::OPERATOR_SUBTRACT:
	case ExpressionType::OPERATOR_MULTIPLY:
	case ExpressionType::OPERATOR_DIVIDE: {
		auto &op = expr.Cast<BoundOperatorExpression>();
		Vector left(op.left->return_type);
		Vector right(op.right->return_type);
		Evaluate(*op.left, chunk, left);
		Evaluate(*op.right, chunk, right);
		for (idx_t i = 0; i < chunk.size(); i++) {
			auto lval = left.GetValue(i);
			auto rval = right.GetValue(i);
			Value out;
			switch (expr.type) {
			case ExpressionType::OPERATOR_ADD:
				out = Value::Add(lval, rval);
				break;
			case ExpressionType::OPERATOR_SUBTRACT:
				out = Value::Subtract(lval, rval);
				break;
			case ExpressionType::OPERATOR_MULTIPLY:
				out = Value::Multiply(lval, rval);
				break;
			default:
				out = Value::Divide(lval, rval);
				break;
			}
			result.SetValue(i, out);
		}
		return;
	}
	default:
		throw ExecutorException("ExpressionExecutor: unsupported expression type");
	}
	// [SOLUTION END]
}

idx_t ExpressionExecutor::Select(const BoundExpression &expr, DataChunk &chunk, SelectionVector &sel) {
	// [SOLUTION BEGIN L3.T1]
	Vector result(LogicalType::Boolean());
	Evaluate(expr, chunk, result);
	idx_t match_count = 0;
	for (idx_t i = 0; i < chunk.size(); i++) {
		auto value = result.GetValue(i);
		if (!value.IsNull() && value.GetBoolean()) {
			sel.set_index(match_count++, i);
		}
	}
	return match_count;
	// [SOLUTION END]
}

} // namespace tiny_duckdb
