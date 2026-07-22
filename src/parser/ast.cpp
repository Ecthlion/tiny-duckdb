#include "tiny_duckdb/parser/ast.hpp"

namespace tiny_duckdb {

std::string ConstantExpression::ToString() const {
	return value.ToString();
}

std::string ColumnRefExpression::ToString() const {
	if (IsQualified()) {
		return table + "." + column;
	}
	return column;
}

std::string ComparisonExpression::ToString() const {
	std::string op;
	switch (type) {
	case ExpressionType::COMPARE_EQUAL:
		op = "=";
		break;
	case ExpressionType::COMPARE_NOT_EQUAL:
		op = "!=";
		break;
	case ExpressionType::COMPARE_LESS_THAN:
		op = "<";
		break;
	case ExpressionType::COMPARE_LESS_THAN_OR_EQUAL:
		op = "<=";
		break;
	case ExpressionType::COMPARE_GREATER_THAN:
		op = ">";
		break;
	case ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL:
		op = ">=";
		break;
	default:
		op = "?";
	}
	return "(" + left->ToString() + " " + op + " " + right->ToString() + ")";
}

std::string ConjunctionExpression::ToString() const {
	const std::string op = type == ExpressionType::CONJUNCTION_AND ? "AND" : "OR";
	return "(" + left->ToString() + " " + op + " " + right->ToString() + ")";
}

std::string OperatorExpression::ToString() const {
	std::string op;
	switch (type) {
	case ExpressionType::OPERATOR_ADD:
		op = "+";
		break;
	case ExpressionType::OPERATOR_SUBTRACT:
		op = "-";
		break;
	case ExpressionType::OPERATOR_MULTIPLY:
		op = "*";
		break;
	case ExpressionType::OPERATOR_DIVIDE:
		op = "/";
		break;
	default:
		op = "?";
	}
	return "(" + left->ToString() + " " + op + " " + right->ToString() + ")";
}

std::string FunctionExpression::ToString() const {
	std::string result = name + "(";
	if (is_star) {
		return result + "*)";
	}
	for (idx_t i = 0; i < args.size(); i++) {
		if (i > 0) {
			result += ", ";
		}
		result += args[i]->ToString();
	}
	return result + ")";
}

std::string StarExpression::ToString() const {
	return "*";
}

} // namespace tiny_duckdb
