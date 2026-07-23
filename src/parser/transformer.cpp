#include "tiny_duckdb/parser/transformer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 2 (Tasks L2.T5 - L2.T7) - the Transformer: parse tree -> AST
//!
//! The PEG parser produces a generic parse tree (peg::Ast: a name, a token
//! string, and children). The Transformer pattern-matches that tree into the
//! typed AST nodes of ast.hpp (SelectStatement, ComparisonExpression, ...).
//!
//! Useful peg::Ast helpers (see parser/peg.hpp):
//!   node.name                  the grammar rule that produced this node
//!   node.token                 the matched source text (terminals only)
//!   node.Find("Rule")          first direct child produced by Rule, or
//!                              nullptr when absent (use it for OPTIONAL
//!                              clauses: WhereClause?, LimitClause?, ...)
//!   node.FindAll("Rule")       all direct children produced by Rule (for
//!                              repeating rules: SelectItem, OrderItem, ...)
//!
//! Work top-down: the three Transform*Op helpers above are already written -
//! read them to learn the matching style before writing your own.
//! ============================================================================

static std::string Lowercase(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
	return text;
}

ExpressionType Transformer::TransformComparisonOp(const std::string &op) {
	if (op == "=") {
		return ExpressionType::COMPARE_EQUAL;
	}
	if (op == "!=" || op == "<>") {
		return ExpressionType::COMPARE_NOT_EQUAL;
	}
	if (op == "<") {
		return ExpressionType::COMPARE_LESS_THAN;
	}
	if (op == "<=") {
		return ExpressionType::COMPARE_LESS_THAN_OR_EQUAL;
	}
	if (op == ">") {
		return ExpressionType::COMPARE_GREATER_THAN;
	}
	if (op == ">=") {
		return ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL;
	}
	throw ParserException("Unknown comparison operator: " + op);
}

ExpressionType Transformer::TransformArithmeticOp(const std::string &op) {
	if (op == "+") {
		return ExpressionType::OPERATOR_ADD;
	}
	if (op == "-") {
		return ExpressionType::OPERATOR_SUBTRACT;
	}
	if (op == "*") {
		return ExpressionType::OPERATOR_MULTIPLY;
	}
	if (op == "/") {
		return ExpressionType::OPERATOR_DIVIDE;
	}
	throw ParserException("Unknown arithmetic operator: " + op);
}

//! ----------------------------------------------------------------------------
//! Task L2.T5a - Transformer::TransformLiteral
//!
//! Turn a Literal parse node into a ConstantExpression with the right Value
//! type. The grammar (sql_grammar.cpp) is:
//!   Literal  <- Number / String / 'true' / 'false' / 'null'
//!   Number   <- [0-9]+ ('.' [0-9]+)?
//!   String   <- ['] (!['] .)* [']
//!
//! Rules: a Number WITHOUT a dot is Value::Integer (std::stoi), with a dot it
//! is Value::Double (std::stod); a String node keeps its quotes in the token
//! - strip them; 'true'/'false'/'null' appear as raw tokens.
//!
//! Tests: Lab2ParserTest.InsertMultipleRows / StringLiteralWithSpaces
//! ----------------------------------------------------------------------------
std::unique_ptr<Expression> Transformer::TransformLiteral(const peg::Ast &node) {
	// TODO(L2.T5): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L2.T5 not implemented yet");
}

//! ----------------------------------------------------------------------------
//! Task L2.T5b - Transformer::TransformExpression
//!
//! The recursive heart of the transformer. Dispatch on node.name:
//!   OrExpr / AndExpr     left-associative chains: fold children pairwise
//!                        into ConjunctionExpression
//!   ComparisonExpr       0 or 1 comparison: a lone AdditiveExpr passes
//!                        through unchanged, otherwise build
//!                        ComparisonExpression(left, right)
//!   AdditiveExpr / MultExpr
//!                        left-associative chains of OperatorExpression
//!                        (map the operator token with TransformArithmeticOp)
//!   UnaryExpr            '-x' becomes subtract(0, x); a plain child passes
//!                        through
//!   PrimaryExpr          parenthesized sub-expression: recurse into it
//!   FuncCall             FunctionExpression; '*' sets is_star (count(*))
//!   ColumnRef            ColumnRefExpression; 't.col' sets BOTH table and
//!                        column, a bare identifier only the column
//!   Number / String      delegate to TransformLiteral
//!
//! Tests: Lab2ParserTest.ArithmeticPrecedence / AndBindsTighterThanOr /
//!        ParenthesizedArithmetic / ConjunctionChainIsLeftAssociative /
//!        NegativeLiteral / ComparisonWithArithmetic
//! ----------------------------------------------------------------------------
std::unique_ptr<Expression> Transformer::TransformExpression(const peg::Ast &node) {
	// TODO(L2.T5): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L2.T5 not implemented yet");
}

//! ----------------------------------------------------------------------------
//! Task L2.T6 - Transformer::TransformSelect
//!
//! Fill a SelectStatement from a SelectStmt parse node:
//!   SelectList    each SelectItem is either Star (-> StarExpression) or an
//!                 Expression with an optional Alias (stored in
//!                 select_aliases, one alias string per select item, "" when
//!                 absent)
//!   FromClause    the table name; JoinClause? fills join_table and
//!                 join_condition (recurse with TransformExpression) and sets
//!                 has_join
//!   WhereClause?  the predicate (optional!)
//!   GroupByClause?  the group key expressions (optional)
//!   OrderByClause?  OrderItem list: expression + ascending flag (DESC sets
//!                 it to false, ASC or absent to true)
//!   LimitClause?  has_limit + limit value (std::stoll on the Number token)
//!
//! Hint: every optional clause is found with node.Find("WhereClause") etc.,
//!       nullptr meaning "absent" - no need to guess from child counts.
//!
//! Tests: Lab2ParserTest.SelectStar / SelectColumnsWithAlias / JoinOn /
//!        GroupByOrderByLimit / MultipleOrderKeys / WhereComparison
//! ----------------------------------------------------------------------------
std::unique_ptr<SelectStatement> Transformer::TransformSelect(const peg::Ast &node) {
	// TODO(L2.T6): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L2.T6 not implemented yet");
}

//! ----------------------------------------------------------------------------
//! Task L2.T7 - Transformer::TransformCreateTable / TransformInsert
//!
//! TransformCreateTable: table name plus one ColumnDefinition per ColumnDef;
//! map the TypeName token (lowercase it first) to a LogicalType:
//!   integer/int -> Integer, bigint -> BigInt, double/real -> Double,
//!   varchar/text -> Varchar, boolean/bool -> Boolean; anything else is a
//!   ParserException (the tests check this for type "blob").
//!
//! TransformInsert: table name plus a vector of value rows; every value is a
//! Literal -> ConstantExpression via TransformLiteral.
//!
//! Tests: Lab2ParserTest.CreateTable / InsertMultipleRows / SyntaxErrorThrows
//! ----------------------------------------------------------------------------
std::unique_ptr<CreateTableStatement> Transformer::TransformCreateTable(const peg::Ast &node) {
	// TODO(L2.T7): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L2.T7 not implemented yet");
}

std::unique_ptr<InsertStatement> Transformer::TransformInsert(const peg::Ast &node) {
	// TODO(L2.T7): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L2.T7 not implemented yet");
}

std::unique_ptr<Statement> Transformer::TransformStatement(const peg::Ast &statement) {
	if (statement.children.empty()) {
		throw ParserException("Empty statement parse tree");
	}
	const peg::Ast &child = *statement.children[0];
	if (child.name == "SelectStmt") {
		return TransformSelect(child);
	}
	if (child.name == "CreateTableStmt") {
		return TransformCreateTable(child);
	}
	if (child.name == "InsertStmt") {
		return TransformInsert(child);
	}
	throw ParserException("Unknown statement node: " + child.name);
}

} // namespace tiny_duckdb
