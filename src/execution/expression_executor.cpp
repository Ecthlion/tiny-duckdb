#include "tiny_duckdb/execution/expression_executor.hpp"

#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/common/vector_operations.hpp"

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

static bool EvaluateComparison(ExpressionType comparison, const Value& left, const Value& right) {
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

//! LAB 5 - TASK #3: evaluate two VECTOR children once per DataChunk, then run
//! the selected distance kernel row by row.
static void EvaluateVectorDistance(const BoundVectorDistanceExpression& expr, DataChunk& chunk, Vector& result) {
	// TODO(L5.T3): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L5.T3 not implemented yet");
}

void ExpressionExecutor::Evaluate(const BoundExpression& expr, DataChunk& chunk, Vector& result) {
	// TODO(L3.T1): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T1 not implemented yet");
}

idx_t ExpressionExecutor::Select(const BoundExpression& expr, DataChunk& chunk, SelectionVector& sel) {
	// TODO(L3.T1): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T1 not implemented yet");
}

} // namespace tiny_duckdb
