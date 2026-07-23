#include "tiny_duckdb/binder/binder.hpp"

#include <set>
#include <utility>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

static std::string Normalize(const std::string &name) {
	return Catalog::NormalizeName(name);
}

idx_t BindScope::Resolve(const ColumnRefExpression &ref) const {
	const std::string column = Normalize(ref.column);
	if (ref.IsQualified()) {
		const std::string table = Normalize(ref.table);
		for (idx_t i = 0; i < names.size(); i++) {
			if (names[i] == column && tables[i] == table) {
				return i;
			}
		}
		throw BinderException("Column not found: " + ref.ToString());
	}
	idx_t found = static_cast<idx_t>(-1);
	for (idx_t i = 0; i < names.size(); i++) {
		if (names[i] == column) {
			if (found != static_cast<idx_t>(-1)) {
				throw BinderException("Ambiguous column reference: " + ref.column +
				                      " (qualify it with a table name)");
			}
			found = i;
		}
	}
	if (found == static_cast<idx_t>(-1)) {
		throw BinderException("Column not found: " + ref.column);
	}
	return found;
}

namespace {

bool IsAggregateName(const std::string &name) {
	const std::string lowered = Normalize(name);
	return lowered == "count" || lowered == "sum" || lowered == "avg" || lowered == "min" || lowered == "max";
}

bool IsVectorDistanceName(const std::string &name) {
	const std::string lowered = Normalize(name);
	return lowered == "l2_distance" || lowered == "array_distance" ||
	       lowered == "cosine_distance" || lowered == "array_cosine_distance" ||
	       lowered == "negative_inner_product" || lowered == "array_negative_inner_product";
}

bool ContainsAggregate(Expression &expression) {
	if (expression.type == ExpressionType::AGGREGATE_COUNT) {
		// FunctionExpression stores AGGREGATE_COUNT as a placeholder type
		auto &function = static_cast<FunctionExpression &>(expression);
		if (IsAggregateName(function.name)) {
			return true;
		}
		if (IsVectorDistanceName(function.name)) {
			return false;
		}
		throw BinderException("Unknown function: " + function.name);
	}
	if (expression.type >= ExpressionType::COMPARE_EQUAL &&
	    expression.type <= ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL) {
		auto &comparison = static_cast<ComparisonExpression &>(expression);
		return ContainsAggregate(*comparison.left) || ContainsAggregate(*comparison.right);
	}
	if (expression.type == ExpressionType::CONJUNCTION_AND || expression.type == ExpressionType::CONJUNCTION_OR) {
		auto &conjunction = static_cast<ConjunctionExpression &>(expression);
		return ContainsAggregate(*conjunction.left) || ContainsAggregate(*conjunction.right);
	}
	if (expression.type >= ExpressionType::OPERATOR_ADD && expression.type <= ExpressionType::OPERATOR_DIVIDE) {
		auto &op = static_cast<OperatorExpression &>(expression);
		return ContainsAggregate(*op.left) || ContainsAggregate(*op.right);
	}
	return false;
}

void SplitConjunction(Expression &expression, std::vector<Expression *> &conjuncts) {
	if (expression.type == ExpressionType::CONJUNCTION_AND) {
		auto &conjunction = static_cast<ConjunctionExpression &>(expression);
		SplitConjunction(*conjunction.left, conjuncts);
		SplitConjunction(*conjunction.right, conjuncts);
		return;
	}
	conjuncts.push_back(&expression);
}

} // namespace

//! LAB 5 - TASK #2: bind a scalar vector-distance function. This is where
//! arity, VECTOR argument types, and equal dimensions are checked before any
//! rows are scanned.
std::unique_ptr<BoundExpression> Binder::BindVectorDistance(FunctionExpression &function,
                                                            const BindScope &scope) {
	// TODO(L5.T2): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L5.T2 not implemented yet");
}

//! ----------------------------------------------------------------------------
//! Task L2.T8a - Binder::BindExpression
//!
//! Bind one AST expression against a BindScope (the columns produced by the
//! FROM clause), producing a BoundExpression with a resolved type:
//!   VALUE_CONSTANT      -> BoundConstantExpression
//!   COLUMN_REF          -> BoundColumnRefExpression via scope.Resolve (it
//!                          throws BinderException on unknown or AMBIGUOUS
//!                          names - do not catch it, that is the test)
//!   COMPARE_*           -> bind both sides; the result is Boolean
//!   CONJUNCTION_AND/OR  -> bind both sides; the result is Boolean
//!   OPERATOR_+-*/       -> both operands must be numeric
//!                          (BinderException otherwise); the result type is
//!                          Value::MaxNumericType(left, right), except
//!                          DIVIDE which is always Double
//!   aggregate functions -> they NEVER appear here (see BindSelect): reaching
//!                          one means it sits in an illegal position such as
//!                          a WHERE clause - throw BinderException
//!
//! Tests: Lab2BinderTest.BindUnknownColumnThrows / BindAmbiguousColumnThrows /
//!        BindQualifiedResolvesAmbiguity / BindArithmeticTypePromotion
//! ----------------------------------------------------------------------------
std::unique_ptr<BoundExpression> Binder::BindExpression(Expression &expression, const BindScope &scope) {
	// TODO(L2.T8): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L2.T8 not implemented yet");
}

//! ----------------------------------------------------------------------------
//! Task L2.T8b - Binder::BindAggregate
//!
//! Bind one aggregate FunctionExpression (count/sum/avg/min/max) into a
//! BoundAggregateExpression. Rules:
//!   count(*)        -> AGGREGATE_COUNT_STAR, no child, returns BigInt
//!   count(col)      -> AGGREGATE_COUNT, returns BigInt
//!   sum / avg       -> numeric child only (BinderException otherwise),
//!                      return Double
//!   min / max       -> any type, returns the CHILD's type
//!   unknown name    -> BinderException
//!
//! Tests: Lab2BinderTest.BindAggregateTypes / BindAggregateRewrite
//! ----------------------------------------------------------------------------
std::unique_ptr<BoundAggregateExpression> Binder::BindAggregate(FunctionExpression &function,
                                                                const BindScope &scope) {
	// TODO(L2.T8): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L2.T8 not implemented yet");
}

//! ----------------------------------------------------------------------------
//! Task L2.T8c - Binder::RewriteAfterAggregate
//!
//! THE key transformation of the binder. A select list like
//!   SELECT l_returnflag, sum(l_quantity) + 1 ... GROUP BY l_returnflag
//! mixes three kinds of sub-expressions; the LogicalAggregate can only
//! produce [group keys..., aggregate results...], so this function rewrites
//! each select-list expression to run ABOVE the aggregation:
//!   * a sub-expression equal to a GROUP BY key  -> BoundColumnRefExpression
//!     pointing at that group's output slot
//!   * an aggregate function                     -> collected into
//!     `aggregates`, replaced by a BoundColumnRefExpression pointing at its
//!     output slot (group_count + aggregate_index)
//!   * anything else (constants, arithmetic over the two above) -> rebuilt
//!     recursively with the same operator and rewritten children
//!
//! Hint: compare a column ref with a GROUP BY key by their NORMALIZED table
//!       and column names (the provided Normalize() helper); a ref that
//!       matches no group key is a BinderException ("must appear in the
//!       GROUP BY clause or inside an aggregate").
//!
//! Tests: Lab2BinderTest.BindAggregateRewrite / BindAggregateArithmeticRewrite
//! ----------------------------------------------------------------------------
std::unique_ptr<BoundExpression> Binder::RewriteAfterAggregate(
    Expression &expression, const std::vector<Expression *> &group_asts,
    const std::vector<LogicalType> &group_types,
    std::vector<std::unique_ptr<BoundAggregateExpression>> &aggregates, const BindScope &scope) {
	// TODO(L2.T8): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L2.T8 not implemented yet");
}

//! ----------------------------------------------------------------------------
//! Task L2.T8d - Binder::BindSelect
//!
//! Assemble the logical plan for a SELECT, bottom-up:
//!   LogicalLimit?            (only with LIMIT)
//!    └ LogicalOrder?         (only with ORDER BY; keys are (output column
//!    └─index, ascending) pairs resolved against the SELECT LIST, not the
//!      base table)
//!     └ LogicalProjection    (the select list)
//!      └ LogicalAggregate?   (only with GROUP BY or any aggregate function)
//!       └ LogicalFilter?     (only with WHERE)
//!        └ LogicalGet | LogicalJoin
//!
//! Steps: bind FROM (LogicalGet over the catalog table, or LogicalJoin with
//! the equi-condition split by SplitConjunction into left/right key pairs -
//! reject non-equi or same-side conditions with BinderException) -> build the
//! BindScope -> WHERE -> detect aggregates (ContainsAggregate) and either
//! bind the select list directly or through RewriteAfterAggregate -> ORDER
//! BY -> LIMIT. Fill the BoundStatement's output names (alias if present,
//! else "table.column" for column refs) and types along the way.
//!
//! Tests: Lab2BinderTest.BindSimpleSelect / BindStarExpands /
//!        BindWhereProducesFilter / BindOrderAndLimit / BindMultipleGroupKeys
//! ----------------------------------------------------------------------------
std::unique_ptr<BoundStatement> Binder::BindSelect(SelectStatement &statement) {
	// TODO(L2.T8): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L2.T8 not implemented yet");
}

std::unique_ptr<BoundStatement> Binder::BindCreateTable(CreateTableStatement &statement) {
	auto result = std::make_unique<BoundStatement>(StatementType::CREATE_TABLE_STATEMENT);
	if (statement.columns.empty()) {
		throw BinderException("CREATE TABLE requires at least one column");
	}
	std::set<std::string> seen;
	for (const auto &column : statement.columns) {
		if (!seen.insert(Normalize(column.name)).second) {
			throw BinderException("Duplicate column name: " + column.name);
		}
	}
	result->table_name = statement.table;
	result->columns = statement.columns;
	return result;
}

//! ----------------------------------------------------------------------------
//! Task L2.T8e - Binder::BindInsert
//!
//! Bind an INSERT: look the table up in the catalog, check the row width
//! against the column count, and coerce every literal Value to the target
//! column type:
//!   * numeric -> numeric: widen via GetNumeric (an INTEGER literal into a
//!     DOUBLE column is fine, and vice versa);
//!   * VARCHAR -> VARCHAR passes through;
//!   * 'null' becomes a typed NULL for the column;
//!   * everything else (e.g. a string into an INTEGER column) is a
//!     BinderException.
//!
//! Tests: Lab2BinderTest.BindInsertCoercesTypes
//! ----------------------------------------------------------------------------
std::unique_ptr<BoundStatement> Binder::BindInsert(InsertStatement &statement) {
	auto result = std::make_unique<BoundStatement>(StatementType::INSERT_STATEMENT);
	TableData &table = catalog_.GetTable(statement.table);
	for (const auto &row : statement.rows) {
		if (row.size() != table.ColumnCount()) {
			throw BinderException("INSERT row has " + std::to_string(row.size()) + " values but table " +
			                      statement.table + " has " + std::to_string(table.ColumnCount()) + " columns");
		}
		std::vector<Value> values;
		for (idx_t col = 0; col < row.size(); col++) {
			if (row[col]->type != ExpressionType::VALUE_CONSTANT) {
				throw BinderException("INSERT only supports literal values");
			}
			const Value &value = static_cast<ConstantExpression &>(*row[col]).value;
			const LogicalType &target = table.GetColumnTypes()[col];
			if (value.IsNull()) {
				values.push_back(Value::Null(target));
				continue;
			}
			switch (target.Id()) {
			case LogicalTypeId::INTEGER:
				if (value.GetType().Id() != LogicalTypeId::INTEGER) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into INTEGER column");
				}
				values.push_back(value);
				break;
			case LogicalTypeId::BIGINT:
				if (!value.GetType().IsIntegral()) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into BIGINT column");
				}
				values.push_back(Value::BigInt(value.GetIntegral()));
				break;
			case LogicalTypeId::DOUBLE:
				if (!value.GetType().IsNumeric()) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into DOUBLE column");
				}
				values.push_back(Value::Double(value.GetNumeric()));
				break;
			case LogicalTypeId::VARCHAR:
				if (value.GetType().Id() != LogicalTypeId::VARCHAR) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into VARCHAR column");
				}
				values.push_back(value);
				break;
			case LogicalTypeId::BOOLEAN:
				if (value.GetType().Id() != LogicalTypeId::BOOLEAN) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into BOOLEAN column");
				}
				values.push_back(value);
				break;
			case LogicalTypeId::VECTOR:
				if (value.GetType() != target) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into " +
					                      target.ToString() + " column");
				}
				values.push_back(value);
				break;
			}
		}
		result->rows.push_back(std::move(values));
	}
	result->insert_table = &table;
	return result;
}

std::unique_ptr<BoundStatement> Binder::Bind(Statement &statement) {
	switch (statement.type) {
	case StatementType::SELECT_STATEMENT:
		return BindSelect(static_cast<SelectStatement &>(statement));
	case StatementType::CREATE_TABLE_STATEMENT:
		return BindCreateTable(static_cast<CreateTableStatement &>(statement));
	case StatementType::INSERT_STATEMENT:
		return BindInsert(static_cast<InsertStatement &>(statement));
	}
	throw BinderException("Unknown statement type");
}

} // namespace tiny_duckdb
