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
	// [SOLUTION BEGIN L2.T5]
	if (!node.children.empty()) {
		const peg::Ast &child = *node.children[0];
		if (child.name == "VectorLiteral") {
			std::vector<double> elements;
			for (const peg::Ast *element : child.FindAll("VectorElement")) {
				const peg::Ast *number = element->Find("Number");
				if (number == nullptr) {
					throw ParserException("Malformed VECTOR element");
				}
				const bool negative = element->token.find('-') != std::string::npos;
				const double value = std::stod(number->token);
				elements.push_back(negative ? -value : value);
			}
			return std::make_unique<ConstantExpression>(Value::Vector(std::move(elements)));
		}
		if (child.name == "Number") {
			if (child.token.find('.') != std::string::npos) {
				return std::make_unique<ConstantExpression>(Value::Double(std::stod(child.token)));
			}
			const int64_t parsed = std::stoll(child.token);
			if (parsed > INT32_MAX) {
				return std::make_unique<ConstantExpression>(Value::BigInt(parsed));
			}
			return std::make_unique<ConstantExpression>(Value::Integer(static_cast<int32_t>(parsed)));
		}
		if (child.name == "String") {
			// strip the surrounding quotes
			return std::make_unique<ConstantExpression>(Value::Varchar(child.token.substr(1, child.token.size() - 2)));
		}
		throw ParserException("Unexpected literal child: " + child.name);
	}
	const std::string keyword = Lowercase(node.token);
	if (keyword == "true") {
		return std::make_unique<ConstantExpression>(Value::Boolean(true));
	}
	if (keyword == "false") {
		return std::make_unique<ConstantExpression>(Value::Boolean(false));
	}
	if (keyword == "null") {
		return std::make_unique<ConstantExpression>(Value::Null(LogicalType::Integer()));
	}
	throw ParserException("Unknown literal: " + node.token);
	// [SOLUTION END]
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
	// [SOLUTION BEGIN L2.T5]
	if (node.name == "Expression") {
		return TransformExpression(*node.children[0]);
	}
	if (node.name == "OrExpr" || node.name == "AndExpr") {
		// children: operand (operand)* - fold left into conjunctions
		const ExpressionType conjunction = node.name == "OrExpr" ? ExpressionType::CONJUNCTION_OR
		                                                         : ExpressionType::CONJUNCTION_AND;
		auto result = TransformExpression(*node.children[0]);
		for (idx_t i = 1; i < node.children.size(); i++) {
			auto right = TransformExpression(*node.children[i]);
			result = std::make_unique<ConjunctionExpression>(conjunction, std::move(result), std::move(right));
		}
		return result;
	}
	if (node.name == "ComparisonExpr") {
		auto left = TransformExpression(*node.children[0]);
		const peg::Ast *op = node.Find("ComparisonOp");
		if (op == nullptr) {
			return left;
		}
		auto right = TransformExpression(*node.children[2]);
		return std::make_unique<ComparisonExpression>(TransformComparisonOp(op->token), std::move(left),
		                                              std::move(right));
	}
	if (node.name == "AdditiveExpr" || node.name == "MultExpr") {
		// children alternate: operand, op, operand, op, operand ...
		auto result = TransformExpression(*node.children[0]);
		for (idx_t i = 1; i + 1 < node.children.size(); i += 2) {
			const ExpressionType arithmetic = TransformArithmeticOp(node.children[i]->token);
			auto right = TransformExpression(*node.children[i + 1]);
			result = std::make_unique<OperatorExpression>(arithmetic, std::move(result), std::move(right));
		}
		return result;
	}
	if (node.name == "UnaryExpr") {
		if (node.children[0]->name == "UnaryExpr") {
			// -x becomes (0 - x)
			auto inner = TransformExpression(*node.children[0]);
			auto zero = std::make_unique<ConstantExpression>(Value::Integer(0));
			return std::make_unique<OperatorExpression>(ExpressionType::OPERATOR_SUBTRACT, std::move(zero),
			                                            std::move(inner));
		}
		return TransformExpression(*node.children[0]);
	}
	if (node.name == "PrimaryExpr") {
		return TransformExpression(*node.children[0]);
	}
	if (node.name == "FuncCall") {
		auto function = std::make_unique<FunctionExpression>(Lowercase(node.Find("Identifier")->token));
		if (node.Find("Star") != nullptr) {
			function->is_star = true;
		}
		for (const peg::Ast *arg : node.FindAll("Expression")) {
			function->args.push_back(TransformExpression(*arg));
		}
		return function;
	}
	if (node.name == "Literal") {
		return TransformLiteral(node);
	}
	if (node.name == "ColumnRef") {
		const auto identifiers = node.FindAll("Identifier");
		if (identifiers.size() == 1) {
			return std::make_unique<ColumnRefExpression>(identifiers[0]->token);
		}
		return std::make_unique<ColumnRefExpression>(identifiers[0]->token, identifiers[1]->token);
	}
	if (node.name == "Star") {
		return std::make_unique<StarExpression>();
	}
	if (node.name == "Number" || node.name == "String") {
		return TransformLiteral(node);
	}
	throw ParserException("Cannot transform expression node: " + node.name);
	// [SOLUTION END]
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
	// [SOLUTION BEGIN L2.T6]
	auto statement = std::make_unique<SelectStatement>();

	// select list
	const peg::Ast &select_list = *node.Find("SelectList");
	for (const peg::Ast *item : select_list.FindAll("SelectItem")) {
		if (item->Find("Star") != nullptr) {
			statement->select_list.push_back(std::make_unique<StarExpression>());
			statement->select_aliases.push_back("");
			continue;
		}
		statement->select_list.push_back(TransformExpression(*item->Find("Expression")));
		const peg::Ast *alias = item->Find("Alias");
		statement->select_aliases.push_back(alias == nullptr ? "" : alias->Find("Identifier")->token);
	}

	// FROM (+ optional JOIN)
	const peg::Ast &table_ref = *node.Find("FromClause")->Find("TableRef");
	statement->table = table_ref.Find("Identifier")->token;
	const peg::Ast *join = table_ref.Find("JoinClause");
	if (join != nullptr) {
		statement->has_join = true;
		statement->join_table = join->Find("Identifier")->token;
		statement->join_condition = TransformExpression(*join->Find("Expression"));
	}

	// WHERE
	const peg::Ast *where = node.Find("WhereClause");
	if (where != nullptr) {
		statement->where = TransformExpression(*where->Find("Expression"));
	}

	// GROUP BY
	const peg::Ast *group_by = node.Find("GroupByClause");
	if (group_by != nullptr) {
		for (const peg::Ast *group : group_by->FindAll("Expression")) {
			statement->group_by.push_back(TransformExpression(*group));
		}
	}

	// ORDER BY
	const peg::Ast *order_by = node.Find("OrderByClause");
	if (order_by != nullptr) {
		for (const peg::Ast *item : order_by->FindAll("OrderItem")) {
			OrderByItem order_item;
			order_item.expression = TransformExpression(*item->Find("Expression"));
			const peg::Ast *sort_order = item->Find("SortOrder");
			order_item.ascending = sort_order == nullptr || Lowercase(sort_order->token) != "desc";
			statement->order_by.push_back(std::move(order_item));
		}
	}

	// LIMIT
	const peg::Ast *limit = node.Find("LimitClause");
	if (limit != nullptr) {
		statement->has_limit = true;
		statement->limit = std::stoll(limit->Find("Number")->token);
	}
	return statement;
	// [SOLUTION END]
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
	// [SOLUTION BEGIN L2.T7]
	auto statement = std::make_unique<CreateTableStatement>();
	statement->table = node.Find("Identifier")->token;
	for (const peg::Ast *definition : node.FindAll("ColumnDef")) {
		ColumnDefinition column;
		column.name = definition->Find("Identifier")->token;
		const peg::Ast *type_node = definition->Find("TypeName");
		const peg::Ast *vector_type = type_node->Find("VectorType");
		const std::string type_name = Lowercase(type_node->token);
		if (vector_type != nullptr) {
			const idx_t dimension = std::stoull(vector_type->Find("ArraySize")->token);
			column.type = LogicalType::Vector(dimension);
		} else if (type_name == "integer" || type_name == "int") {
			column.type = LogicalType::Integer();
		} else if (type_name == "bigint") {
			column.type = LogicalType::BigInt();
		} else if (type_name == "double" || type_name == "real") {
			column.type = LogicalType::Double();
		} else if (type_name == "varchar" || type_name == "text") {
			column.type = LogicalType::Varchar();
		} else if (type_name == "boolean" || type_name == "bool") {
			column.type = LogicalType::Boolean();
		} else {
			throw ParserException("Unknown column type: " + type_name);
		}
		statement->columns.push_back(std::move(column));
	}
	return statement;
	// [SOLUTION END]
}

std::unique_ptr<InsertStatement> Transformer::TransformInsert(const peg::Ast &node) {
	// [SOLUTION BEGIN L2.T7]
	auto statement = std::make_unique<InsertStatement>();
	statement->table = node.Find("Identifier")->token;
	for (const peg::Ast *row : node.Find("RowList")->FindAll("Row")) {
		std::vector<std::unique_ptr<Expression>> values;
		for (const peg::Ast *literal : row->FindAll("Literal")) {
			values.push_back(TransformLiteral(*literal));
		}
		statement->rows.push_back(std::move(values));
	}
	return statement;
	// [SOLUTION END]
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
