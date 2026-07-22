#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/common/value.hpp"

namespace tiny_duckdb {

//! ---------------------------------------------------------------------------
//! Parsed SQL expressions (unbound: names not resolved yet)
//! ---------------------------------------------------------------------------
class Expression {
public:
	virtual ~Expression() = default;

	ExpressionType type;
	virtual std::string ToString() const = 0;

protected:
	explicit Expression(ExpressionType type) : type(type) {
	}
};

class ConstantExpression : public Expression {
public:
	explicit ConstantExpression(Value value_p) : Expression(ExpressionType::VALUE_CONSTANT), value(std::move(value_p)) {
	}

	Value value;

	std::string ToString() const override;
};

//! column, or table.column
class ColumnRefExpression : public Expression {
public:
	explicit ColumnRefExpression(std::string column_name)
	    : Expression(ExpressionType::COLUMN_REF), column(std::move(column_name)) {
	}
	ColumnRefExpression(std::string table_name, std::string column_name)
	    : Expression(ExpressionType::COLUMN_REF), table(std::move(table_name)), column(std::move(column_name)) {
	}

	bool IsQualified() const {
		return !table.empty();
	}

	std::string table;
	std::string column;

	std::string ToString() const override;
};

class ComparisonExpression : public Expression {
public:
	ComparisonExpression(ExpressionType comparison, std::unique_ptr<Expression> left_p,
	                     std::unique_ptr<Expression> right_p)
	    : Expression(comparison), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::unique_ptr<Expression> left;
	std::unique_ptr<Expression> right;

	std::string ToString() const override;
};

class ConjunctionExpression : public Expression {
public:
	ConjunctionExpression(ExpressionType conjunction, std::unique_ptr<Expression> left_p,
	                      std::unique_ptr<Expression> right_p)
	    : Expression(conjunction), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::unique_ptr<Expression> left;
	std::unique_ptr<Expression> right;

	std::string ToString() const override;
};

//! Arithmetic: + - * /
class OperatorExpression : public Expression {
public:
	OperatorExpression(ExpressionType op, std::unique_ptr<Expression> left_p, std::unique_ptr<Expression> right_p)
	    : Expression(op), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::unique_ptr<Expression> left;
	std::unique_ptr<Expression> right;

	std::string ToString() const override;
};

//! A function call, e.g. count(*), sum(l_extendedprice). Note: the `type`
//! member carries AGGREGATE_COUNT as a placeholder until the binder resolves it.
class FunctionExpression : public Expression {
public:
	explicit FunctionExpression(std::string name_p) : Expression(ExpressionType::AGGREGATE_COUNT), name(std::move(name_p)) {
	}

	std::string name;
	std::vector<std::unique_ptr<Expression>> args;
	bool is_star = false; // count(*)

	std::string ToString() const override;
};

//! SELECT *
class StarExpression : public Expression {
public:
	StarExpression() : Expression(ExpressionType::COLUMN_REF) {
	}

	std::string ToString() const override;
};

//! ---------------------------------------------------------------------------
//! Statements
//! ---------------------------------------------------------------------------
enum class StatementType : uint8_t { SELECT_STATEMENT, CREATE_TABLE_STATEMENT, INSERT_STATEMENT };

class Statement {
public:
	virtual ~Statement() = default;

	StatementType type;

protected:
	explicit Statement(StatementType type) : type(type) {
	}
};

struct OrderByItem {
	std::unique_ptr<Expression> expression;
	bool ascending = true;
};

class SelectStatement : public Statement {
public:
	SelectStatement() : Statement(StatementType::SELECT_STATEMENT) {
	}

	std::vector<std::unique_ptr<Expression>> select_list;
	//! Alias per select item ("" when absent); parallel to select_list
	std::vector<std::string> select_aliases;

	std::string table;
	//! Optional INNER JOIN (single join)
	bool has_join = false;
	std::string join_table;
	std::unique_ptr<Expression> join_condition;

	std::unique_ptr<Expression> where;

	std::vector<std::unique_ptr<Expression>> group_by;
	std::vector<OrderByItem> order_by;

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
