#include "tdbtest.h"
#include "tiny_duckdb/binder/binder.hpp"
#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/common/vector_operations.hpp"
#include "tiny_duckdb/main/tiny_duckdb.hpp"
#include "tiny_duckdb/parser/parser.hpp"

using namespace tiny_duckdb;

namespace {

std::unique_ptr<BoundStatement> BindVectorSql(Catalog &catalog, const std::string &sql) {
	SqlParser parser;
	auto statement = parser.Parse(sql);
	Binder binder(catalog);
	return binder.Bind(*statement);
}

} // namespace

TEST(Lab5VectorExpressionTest, VectorTypeAndLiteralParsing) {
	SqlParser parser;
	auto create_statement = parser.Parse("CREATE TABLE items (id INTEGER, embedding VECTOR(3))");
	auto &create = static_cast<CreateTableStatement &>(*create_statement);
	ASSERT_EQ(create.columns.size(), 2);
	EXPECT_EQ(create.columns[1].type, LogicalType::Vector(3));
	EXPECT_EQ(create.columns[1].type.ToString(), "VECTOR(3)");

	auto insert_statement = parser.Parse("INSERT INTO items VALUES (1, [1, -2.5, 3])");
	auto &insert = static_cast<InsertStatement &>(*insert_statement);
	auto &constant = static_cast<ConstantExpression &>(*insert.rows[0][1]);
	EXPECT_EQ(constant.value, Value::Vector({1.0, -2.5, 3.0}));
	EXPECT_EQ(constant.value.ToString(), "[1.0, -2.5, 3.0]");
}

TEST(Lab5VectorExpressionTest, DistanceKernels) {
	EXPECT_NEAR(VectorOperations::L2Distance({1, 2, 3}, {1, 2, 5}), 2.0, 1e-12);
	EXPECT_NEAR(VectorOperations::CosineDistance({1, 0}, {1, 0}), 0.0, 1e-12);
	EXPECT_NEAR(VectorOperations::CosineDistance({1, 0}, {0, 1}), 1.0, 1e-12);
	EXPECT_NEAR(VectorOperations::NegativeInnerProduct({1, 2}, {3, 4}), -11.0, 1e-12);
	EXPECT_THROW(VectorOperations::CosineDistance({0, 0}, {1, 0}), ExecutorException);
}

TEST(Lab5VectorExpressionTest, BinderChecksFunctionAndDimension) {
	Catalog catalog;
	catalog.CreateTable("items", {"id", "embedding"}, {LogicalType::Integer(), LogicalType::Vector(3)});

	auto bound = BindVectorSql(catalog,
	                           "SELECT cosine_distance(embedding, [1, 0, 0]) AS distance FROM items");
	EXPECT_EQ(bound->types[0], LogicalType::Double());
	auto &projection = bound->plan->Cast<LogicalProjection>();
	EXPECT_EQ(projection.expressions[0]->type, ExpressionType::VECTOR_DISTANCE);

	EXPECT_THROW(BindVectorSql(catalog, "SELECT l2_distance(embedding, [1, 0]) FROM items"),
	             BinderException);
	EXPECT_THROW(BindVectorSql(catalog, "SELECT cosine_distance(id, [1, 0, 0]) FROM items"),
	             BinderException);
	EXPECT_THROW(BindVectorSql(catalog, "SELECT l2_distance(embedding) FROM items"), BinderException);

	auto duckdb_alias = BindVectorSql(catalog, "SELECT array_distance(embedding, [1, 0, 0]) FROM items");
	EXPECT_EQ(duckdb_alias->types[0], LogicalType::Double());
}

TEST(Lab5VectorExpressionTest, InsertRejectsWrongDimension) {
	TinyDuckDB db;
	Connection connection(db);
	connection.Query("CREATE TABLE items (id INTEGER, embedding VECTOR(3))");
	EXPECT_THROW(connection.Query("INSERT INTO items VALUES (1, [1, 2])"), BinderException);
}

TEST(Lab5VectorExpressionTest, ExactTopKCosineQuery) {
	TinyDuckDB db;
	db.SetThreads(2);
	Connection connection(db);
	connection.Query("CREATE TABLE docs (id INTEGER, title VARCHAR, embedding VECTOR(3))");
	connection.Query(
	    "INSERT INTO docs VALUES "
	    "(1, 'database systems', [1, 0, 0]), "
	    "(2, 'query execution', [0.9, 0.1, 0]), "
	    "(3, 'cooking', [0, 0, 1]), "
	    "(4, 'machine learning', [0.7, 0.3, 0])");

	auto result = connection.Query(
	    "SELECT id, title, cosine_distance(embedding, [1, 0, 0]) AS distance "
	    "FROM docs ORDER BY distance LIMIT 3");
	ASSERT_EQ(result->RowCount(), 3);
	EXPECT_EQ(result->GetValue(0, 0), Value::Integer(1));
	EXPECT_EQ(result->GetValue(0, 1), Value::Integer(2));
	EXPECT_EQ(result->GetValue(0, 2), Value::Integer(4));
	EXPECT_NEAR(result->GetValue(2, 0).GetDouble(), 0.0, 1e-12);
}

TEST(Lab5VectorExpressionTest, L2AndInnerProductQueries) {
	TinyDuckDB db;
	Connection connection(db);
	connection.Query("CREATE TABLE points (id INTEGER, embedding VECTOR(2))");
	connection.Query("INSERT INTO points VALUES (1, [0, 0]), (2, [1, 1]), (3, [3, 3])");

	auto l2 = connection.Query(
	    "SELECT id, l2_distance(embedding, [0.9, 1.1]) AS distance "
	    "FROM points ORDER BY distance LIMIT 1");
	EXPECT_EQ(l2->GetValue(0, 0), Value::Integer(2));
	EXPECT_NEAR(l2->GetValue(1, 0).GetDouble(), std::sqrt(0.02), 1e-12);

	auto inner = connection.Query(
	    "SELECT id, negative_inner_product(embedding, [1, 1]) AS score "
	    "FROM points ORDER BY score LIMIT 1");
	EXPECT_EQ(inner->GetValue(0, 0), Value::Integer(3));
	EXPECT_NEAR(inner->GetValue(1, 0).GetDouble(), -6.0, 1e-12);
}

TEST(Lab5VectorExpressionTest, NullDistancePropagates) {
	TinyDuckDB db;
	Connection connection(db);
	connection.Query("CREATE TABLE points (id INTEGER, embedding VECTOR(2))");
	connection.Query("INSERT INTO points VALUES (1, null), (2, [1, 0])");

	auto result =
	    connection.Query("SELECT id, cosine_distance(embedding, [1, 0]) AS distance FROM points");
	ASSERT_EQ(result->RowCount(), 2);
	EXPECT_TRUE(result->GetValue(1, 0).IsNull());
	EXPECT_NEAR(result->GetValue(1, 1).GetDouble(), 0.0, 1e-12);
}
