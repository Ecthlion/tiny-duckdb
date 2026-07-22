#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tiny_duckdb/binder/bound_expression.hpp"
#include "tiny_duckdb/parser/ast.hpp"
#include "tiny_duckdb/planner/logical_plan.hpp"
#include "tiny_duckdb/storage/catalog.hpp"

namespace tiny_duckdb {

//! A fully-resolved statement ready for planning/execution
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

//! The set of column names visible to a clause (FROM/JOIN outputs)
struct BindScope {
	std::vector<std::string> tables; // source table per column (normalized)
	std::vector<std::string> names;  // column names (normalized)
	std::vector<LogicalType> types;

	//! Resolve a column reference to a position; throws on unknown/ambiguous
	idx_t Resolve(const ColumnRefExpression &ref) const;

	idx_t ColumnCount() const {
		return names.size();
	}
};

//! ============================================================================
//! LAB 2 (Task L2.T8) - the Binder: names -> indexes, types, logical plan
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
	std::unique_ptr<BoundAggregateExpression> BindAggregate(FunctionExpression &function, const BindScope &scope);

	//! Rewrite a select expression over the output of the LogicalAggregate:
	//! group keys become references to group columns, aggregates get collected.
	std::unique_ptr<BoundExpression> RewriteAfterAggregate(
	    Expression &expression, const std::vector<Expression *> &group_asts,
	    const std::vector<LogicalType> &group_types,
	    std::vector<std::unique_ptr<BoundAggregateExpression>> &aggregates, const BindScope &scope);

	Catalog &catalog_;
};

} // namespace tiny_duckdb
