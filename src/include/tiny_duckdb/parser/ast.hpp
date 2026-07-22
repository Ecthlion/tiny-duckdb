#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/common/value.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 2 - the SQL AST produced by the Transformer
//! ============================================================================

//! Base class for all expressions
class Expression {
public:
	explicit Expression(ExpressionType type) : type(type) {
	}
	virtual ~Expression() = default;

	ExpressionType type;

	virtual std::string ToString() const = 0;
};

class ConstantExpression : public Expression {
public:
	explicit ConstantExpression(const Value &value_p) : Expression(ExpressionType::VALUE_CONSTANT), value(value_p) {
	}

	std::string ToString() const override;

	Value value;
};

class ColumnRefExpression : public Expression {
public:
	explicit ColumnRefExpression(std::string column_p)
	    : Expression(ExpressionType::COLUMN_REF), column(std::move(column_p)) {
	}
	ColumnRefExpression(std::string table_p, std::string column_p)
	    : Expression(ExpressionType::COLUMN_REF), table(std::move(table_p)), column(std::move(column_p)) {
	}

	bool IsQualified() const {
		return !table.empty();
	}
	std::string ToString() const override;

	std::string table;
	std::string column;
};

class ComparisonExpression : public Expression {
public:
	ComparisonExpression(ExpressionType type, std::unique_ptr<Expression> left_p, std::unique_ptr<Expression> right_p)
	    : Expression(type), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::string ToString() const override;

	std::unique_ptr<Expression> left;
	std::unique_ptr<Expression> right;
};

class ConjunctionExpression : public Expression {
public:
	ConjunctionExpression(ExpressionType type, std::unique_ptr<Expression> left_p, std::unique_ptr<Expression> right_p)
	    : Expression(type), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::string ToString() const override;

	std::unique_ptr<Expression> left;
	std::unique_ptr<Expression> right;
};

class OperatorExpression : public Expression {
public:
	OperatorExpression(ExpressionType type, std::unique_ptr<Expression> left_p, std::unique_ptr<Expression> right_p)
	    : Expression(type), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::string ToString() const override;

	std::unique_ptr<Expression> left;
	std::unique_ptr<Expression> right;
};

//! A function call like count(*) or sum(l_quantity). The type field holds
//! AGGREGATE_COUNT as a placeholder until the binder resolves the function.
class FunctionExpression : public Expression {
public:
	FunctionExpression(std::string name_p, std::vector<std::unique_ptr<Expression>> args_p, bool is_star_p)
	    : Expression(ExpressionType::AGGREGATE_COUNT), name(std::move(name_p)), args(std::move(args_p)),
	      is_star(is_star_p) {
	}

	std::string ToString() const override;

	std::string name;
	std::vector<std::unique_ptr<Expression>> args;
	bool is_star;
};

//! The bare `*` in a select list
class StarExpression : public Expression {
public:
	StarExpression() : Expression(ExpressionType::COLUMN_REF) {
	}

	std::string ToString() const override;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

enum class StatementType : uint8_t { SELECT_STATEMENT, CREATE_TABLE_STATEMENT, INSERT_STATEMENT };

class Statement {
public:
	explicit Statement(StatementType type) : type(type) {
	}
	virtual ~Statement() = default;

	StatementType type;
};

struct OrderItem {
	std::unique_ptr<Expression> expression;
	bool ascending = true;
};

class SelectStatement : public Statement {
public:
	SelectStatement() : Statement(StatementType::SELECT_STATEMENT) {
	}

	std::vector<std::unique_ptr<Expression>> select_list;
	//! Parallel to select_list; empty string = no alias
	std::vector<std::string> select_aliases;
	std::string table;
	bool has_join = false;
	std::string join_table;
	std::unique_ptr<Expression> join_condition;
	std::unique_ptr<Expression> where;
	std::vector<std::unique_ptr<Expression>> group_by;
	std::vector<OrderItem> order_by;
	bool has_limit = false;
	int64_t limit = 0;
};

struct ColumnDefinition {
	std::string name;
	LogicalType type;
};

class CreateTableStatement : public Statement {
public:
	CreateTableStatement() : Statement(StatementType::CREATE_TABLE_STATEMENT) {
	}

	std::string table;
	std::vector<ColumnDefinition> columns;
};

class InsertStatement : public Statement {
public:
	InsertStatement() : Statement(StatementType::INSERT_STATEMENT) {
	}

	std::string table;
	//! Rows of literal values
	std::vector<std::vector<std::unique_ptr<Expression>>> rows;
};

} // namespace tiny_duckdb
