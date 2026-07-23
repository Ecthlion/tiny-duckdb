#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tiny_duckdb/binder/bound_expression.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/parser/ast.hpp"
#include "tiny_duckdb/planner/logical_plan.hpp"
#include "tiny_duckdb/storage/catalog.hpp"

namespace tiny_duckdb {

//! A fully bound statement: logical plan (SELECT) or direct catalog/data
//! operations (CREATE TABLE / INSERT).
class BoundStatement {
public:
	explicit BoundStatement(StatementType type) : type(type) {
	}

	StatementType type;

	// SELECT
	std::unique_ptr<LogicalOperator> plan;
	std::vector<std::string> names;
	std::vector<LogicalType> types;

	// CREATE TABLE
	std::string table_name;
	std::vector<ColumnDefinition> columns;

	// INSERT
	TableData *insert_table = nullptr;
	std::vector<std::vector<Value>> rows;
};

//! The scope a SELECT binds against: the columns produced by the FROM clause.
struct BindScope {
	std::vector<std::string> tables; // owning table per column (normalized)
	std::vector<std::string> names;  // column name per column (normalized)
	std::vector<LogicalType> types;

	idx_t ColumnCount() const {
		return names.size();
	}
	//! Resolve a column reference to an index; throws BinderException
	idx_t Resolve(const ColumnRefExpression &ref) const;
};

//! ============================================================================
//! LAB 2 (Task L2.T8) - the Binder
//!
//! Resolves table/column names against the catalog, checks types, and builds
//! the logical plan:
//!
//!   LogicalLimit?
//!    └ LogicalOrder?
//!     └ LogicalProjection
//!      └ LogicalAggregate?      (only with GROUP BY / aggregates)
//!       └ LogicalFilter?        (only with WHERE)
//!        └ LogicalGet | LogicalJoin
//! ============================================================================
class Binder {
public:
	explicit Binder(Catalog &catalog) : catalog_(catalog) {
	}

	std::unique_ptr<BoundStatement> Bind(Statement &statement);

private:
	std::unique_ptr<BoundStatement> BindSelect(SelectStatement &statement);
	std::unique_ptr<BoundStatement> BindCreateTable(CreateTableStatement &statement);
	std::unique_ptr<BoundStatement> BindInsert(InsertStatement &statement);

	std::unique_ptr<BoundExpression> BindExpression(Expression &expression, const BindScope &scope);
	std::unique_ptr<BoundExpression> BindVectorDistance(FunctionExpression &function, const BindScope &scope);
	std::unique_ptr<BoundAggregateExpression> BindAggregate(FunctionExpression &function, const BindScope &scope);
	//! Rewrite a select-list expression to run above the aggregation node
	std::unique_ptr<BoundExpression> RewriteAfterAggregate(
	    Expression &expression, const std::vector<Expression *> &group_asts,
	    const std::vector<LogicalType> &group_types,
	    std::vector<std::unique_ptr<BoundAggregateExpression>> &aggregates, const BindScope &scope);

	Catalog &catalog_;
};

} // namespace tiny_duckdb
