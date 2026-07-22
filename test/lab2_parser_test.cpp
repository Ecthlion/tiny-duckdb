#include "tdbtest.h"
#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/parser/parser.hpp"

using namespace tiny_duckdb;

// ---------------------------------------------------------------------------
// LAB 2 - the PEG parser: grammar + transformer
// ---------------------------------------------------------------------------

TEST(Lab2ParserTest, SelectStar) {
	SqlParser parser;
	auto statement = parser.Parse("SELECT * FROM lineitem");
	ASSERT_EQ(statement->type, StatementType::SELECT_STATEMENT);
	auto &select = static_cast<SelectStatement &>(*statement);
	EXPECT_EQ(select.select_list.size(), 1);
	EXPECT_EQ(select.table, "lineitem");
	EXPECT_FALSE(select.has_join);
}

TEST(Lab2ParserTest, SelectColumnsWithAlias) {
	SqlParser parser;
	auto statement = parser.Parse("SELECT l_orderkey AS k, l_quantity FROM lineitem");
	auto &select = static_cast<SelectStatement &>(*statement);
	EXPECT_EQ(select.select_list.size(), 2);
	EXPECT_EQ(select.select_aliases[0], "k");
	EXPECT_EQ(select.select_aliases[1], "");
	auto &ref = static_cast<ColumnRefExpression &>(*select.select_list[0]);
	EXPECT_EQ(ref.column, "l_orderkey");
}

TEST(Lab2ParserTest, WhereComparison) {
	SqlParser parser;
	auto statement = parser.Parse("SELECT a FROM t WHERE a >= 10");
	auto &select = static_cast<SelectStatement &>(*statement);
	ASSERT_TRUE(select.where != nullptr);
	EXPECT_EQ(select.where->type, ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL);
}

TEST(Lab2ParserTest, AndBindsTighterThanOr) {
	SqlParser parser;
	// a = 1 OR b = 2 AND c = 3  ->  OR(a=1, AND(b=2, c=3))
	auto statement = parser.Parse("SELECT a FROM t WHERE a = 1 OR b = 2 AND c = 3");
	auto &select = static_cast<SelectStatement &>(*statement);
	ASSERT_EQ(select.where->type, ExpressionType::CONJUNCTION_OR);
	auto &or_expr = static_cast<ConjunctionExpression &>(*select.where);
	EXPECT_EQ(or_expr.right->type, ExpressionType::CONJUNCTION_AND);
}

TEST(Lab2ParserTest, ArithmeticPrecedence) {
	SqlParser parser;
	// 1 + 2 * 3  ->  add(1, mul(2, 3))
	auto statement = parser.Parse("SELECT 1 + 2 * 3 FROM t");
	auto &select = static_cast<SelectStatement &>(*statement);
	ASSERT_EQ(select.select_list[0]->type, ExpressionType::OPERATOR_ADD);
	auto &add = static_cast<OperatorExpression &>(*select.select_list[0]);
	EXPECT_EQ(add.right->type, ExpressionType::OPERATOR_MULTIPLY);
}

TEST(Lab2ParserTest, GroupByOrderByLimit) {
	SqlParser parser;
	auto statement = parser.Parse(
	    "SELECT f, count(*) FROM t GROUP BY f ORDER BY f DESC LIMIT 5");
	auto &select = static_cast<SelectStatement &>(*statement);
	EXPECT_EQ(select.group_by.size(), 1);
	EXPECT_EQ(select.order_by.size(), 1);
	EXPECT_FALSE(select.order_by[0].ascending);
	EXPECT_TRUE(select.has_limit);
	EXPECT_EQ(select.limit, 5);
	auto &function = static_cast<FunctionExpression &>(*select.select_list[1]);
	EXPECT_EQ(function.name, "count");
	EXPECT_TRUE(function.is_star);
}

TEST(Lab2ParserTest, JoinOn) {
	SqlParser parser;
	auto statement = parser.Parse("SELECT * FROM orders JOIN customer ON orders.o_custkey = customer.o_custkey");
	auto &select = static_cast<SelectStatement &>(*statement);
	EXPECT_EQ(select.table, "orders");
	EXPECT_TRUE(select.has_join);
	EXPECT_EQ(select.join_table, "customer");
	ASSERT_TRUE(select.join_condition != nullptr);
	EXPECT_EQ(select.join_condition->type, ExpressionType::COMPARE_EQUAL);
	auto &cmp = static_cast<ComparisonExpression &>(*select.join_condition);
	auto &left = static_cast<ColumnRefExpression &>(*cmp.left);
	EXPECT_TRUE(left.IsQualified());
	EXPECT_EQ(left.table, "orders");
	EXPECT_EQ(left.column, "o_custkey");
}

TEST(Lab2ParserTest, CreateTable) {
	SqlParser parser;
	auto statement = parser.Parse("CREATE TABLE t (a INTEGER, b DOUBLE, c VARCHAR)");
	ASSERT_EQ(statement->type, StatementType::CREATE_TABLE_STATEMENT);
	auto &create = static_cast<CreateTableStatement &>(*statement);
	EXPECT_EQ(create.table, "t");
	EXPECT_EQ(create.columns.size(), 3);
	EXPECT_EQ(create.columns[1].type.Id(), LogicalTypeId::DOUBLE);
}

TEST(Lab2ParserTest, InsertMultipleRows) {
	SqlParser parser;
	auto statement = parser.Parse("INSERT INTO t VALUES (1, 'a'), (2, 'b'), (3, 'c')");
	ASSERT_EQ(statement->type, StatementType::INSERT_STATEMENT);
	auto &insert = static_cast<InsertStatement &>(*statement);
	EXPECT_EQ(insert.rows.size(), 3);
	EXPECT_EQ(insert.rows[1].size(), 2);
	auto &value = static_cast<ConstantExpression &>(*insert.rows[2][0]);
	EXPECT_EQ(value.value, Value::Integer(3));
}

TEST(Lab2ParserTest, CaseInsensitiveKeywordsAndNames) {
	SqlParser parser;
	auto statement = parser.Parse("select L_ORDERKEY from LINEITEM where L_QUANTITY > 5");
	auto &select = static_cast<SelectStatement &>(*statement);
	EXPECT_EQ(select.table, "LINEITEM");
	ASSERT_TRUE(select.where != nullptr);
	EXPECT_EQ(select.where->type, ExpressionType::COMPARE_GREATER_THAN);
}

TEST(Lab2ParserTest, SyntaxErrorThrows) {
	SqlParser parser;
	EXPECT_THROW(parser.Parse("SELCT 1 FROM t"), ParserException);
	EXPECT_THROW(parser.Parse("SELECT FROM"), ParserException);
	EXPECT_THROW(parser.Parse("CREATE TABLE x (a BLOB)"), ParserException);
}

TEST(Lab2ParserTest, ParseTreeShape) {
	SqlParser parser;
	auto tree = parser.ParseTree("SELECT a FROM t");
	ASSERT_TRUE(tree != nullptr);
	EXPECT_EQ(tree->name, "Statement");
}

TEST(Lab2ParserTest, ParenthesizedArithmetic) {
	SqlParser parser;
	// (1 + 2) * 3  ->  mul(add(1, 2), 3): parentheses override precedence
	auto statement = parser.Parse("SELECT (1 + 2) * 3 FROM t");
	auto &select = static_cast<SelectStatement &>(*statement);
	ASSERT_EQ(select.select_list[0]->type, ExpressionType::OPERATOR_MULTIPLY);
	auto &mul = static_cast<OperatorExpression &>(*select.select_list[0]);
	EXPECT_EQ(mul.left->type, ExpressionType::OPERATOR_ADD);
}

TEST(Lab2ParserTest, ComparisonWithArithmetic) {
	SqlParser parser;
	// a + 1 > 5  ->  gt(add(a, 1), 5): arithmetic binds tighter than comparison
	auto statement = parser.Parse("SELECT a FROM t WHERE a + 1 > 5");
	auto &select = static_cast<SelectStatement &>(*statement);
	ASSERT_EQ(select.where->type, ExpressionType::COMPARE_GREATER_THAN);
	auto &cmp = static_cast<ComparisonExpression &>(*select.where);
	EXPECT_EQ(cmp.left->type, ExpressionType::OPERATOR_ADD);
}

TEST(Lab2ParserTest, ConjunctionChainIsLeftAssociative) {
	SqlParser parser;
	// a = 1 AND b = 2 AND c = 3  ->  AND(AND(a=1, b=2), c=3)
	auto statement = parser.Parse("SELECT a FROM t WHERE a = 1 AND b = 2 AND c = 3");
	auto &select = static_cast<SelectStatement &>(*statement);
	ASSERT_EQ(select.where->type, ExpressionType::CONJUNCTION_AND);
	auto &outer = static_cast<ConjunctionExpression &>(*select.where);
	EXPECT_EQ(outer.left->type, ExpressionType::CONJUNCTION_AND);
	EXPECT_EQ(outer.right->type, ExpressionType::COMPARE_EQUAL);
}

TEST(Lab2ParserTest, StringLiteralWithSpaces) {
	SqlParser parser;
	auto statement = parser.Parse("SELECT s FROM t WHERE s = 'hello world'");
	auto &select = static_cast<SelectStatement &>(*statement);
	ASSERT_EQ(select.where->type, ExpressionType::COMPARE_EQUAL);
	auto &cmp = static_cast<ComparisonExpression &>(*select.where);
	auto &value = static_cast<ConstantExpression &>(*cmp.right);
	EXPECT_EQ(value.value, Value::Varchar("hello world"));
}

TEST(Lab2ParserTest, MultipleOrderKeys) {
	SqlParser parser;
	auto statement = parser.Parse("SELECT a, b FROM t ORDER BY a ASC, b DESC, c");
	auto &select = static_cast<SelectStatement &>(*statement);
	ASSERT_EQ(select.order_by.size(), 3);
	EXPECT_TRUE(select.order_by[0].ascending);
	EXPECT_FALSE(select.order_by[1].ascending);
	EXPECT_TRUE(select.order_by[2].ascending); // default is ASC
}

TEST(Lab2ParserTest, NegativeLiteral) {
	SqlParser parser;
	// -5  ->  subtract(0, 5)
	auto statement = parser.Parse("SELECT -5 FROM t");
	auto &select = static_cast<SelectStatement &>(*statement);
	ASSERT_EQ(select.select_list[0]->type, ExpressionType::OPERATOR_SUBTRACT);
}
