#include "tdbtest.h"
#include "tiny_duckdb/binder/binder.hpp"
#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/parser/parser.hpp"

using namespace tiny_duckdb;

// ---------------------------------------------------------------------------
// LAB 2 - the binder: name resolution, types, logical plan
// ---------------------------------------------------------------------------

namespace {

std::unique_ptr<Catalog> MakeCatalog() {
	auto catalog = std::make_unique<Catalog>();
	catalog->CreateTable("lineitem", {"l_orderkey", "l_quantity", "l_returnflag"},
	                     {LogicalType::Integer(), LogicalType::Double(), LogicalType::Varchar()});
	catalog->CreateTable("orders", {"o_orderkey", "o_custkey"},
	                     {LogicalType::Integer(), LogicalType::Integer()});
	// shares o_custkey with orders: used to test ambiguity
	catalog->CreateTable("customer", {"o_custkey", "c_name"},
	                     {LogicalType::Integer(), LogicalType::Varchar()});
	return catalog;
}

std::unique_ptr<BoundStatement> BindSql(Catalog &catalog, const std::string &sql) {
	SqlParser parser;
	auto statement = parser.Parse(sql);
	Binder binder(catalog);
	return binder.Bind(*statement);
}

} // namespace

TEST(Lab2BinderTest, BindSimpleSelect) {
	auto catalog = MakeCatalog();
	auto bound = BindSql(*catalog, "SELECT l_orderkey, l_quantity FROM lineitem");
	ASSERT_EQ(bound->type, StatementType::SELECT_STATEMENT);
	EXPECT_EQ(bound->names.size(), 2);
	EXPECT_EQ(bound->types[0].Id(), LogicalTypeId::INTEGER);
	EXPECT_EQ(bound->types[1].Id(), LogicalTypeId::DOUBLE);
	// plan: projection over get
	ASSERT_EQ(bound->plan->type, LogicalOperatorType::LOGICAL_PROJECTION);
	ASSERT_EQ(bound->plan->children[0]->type, LogicalOperatorType::LOGICAL_GET);
}

TEST(Lab2BinderTest, BindStarExpands) {
	auto catalog = MakeCatalog();
	auto bound = BindSql(*catalog, "SELECT * FROM orders");
	EXPECT_EQ(bound->names.size(), 2);
	EXPECT_EQ(bound->names[0], "orders.o_orderkey");
	EXPECT_EQ(bound->names[1], "orders.o_custkey");
}

TEST(Lab2BinderTest, BindUnknownColumnThrows) {
	auto catalog = MakeCatalog();
	EXPECT_THROW(BindSql(*catalog, "SELECT nope FROM lineitem"), BinderException);
}

TEST(Lab2BinderTest, BindUnknownTableThrows) {
	auto catalog = MakeCatalog();
	EXPECT_THROW(BindSql(*catalog, "SELECT a FROM nope"), CatalogException);
}

TEST(Lab2BinderTest, BindAmbiguousColumnThrows) {
	auto catalog = MakeCatalog();
	// o_custkey exists in both orders and customer
	EXPECT_THROW(BindSql(*catalog,
	                     "SELECT o_custkey FROM orders JOIN customer ON orders.o_custkey = customer.o_custkey"),
	             BinderException);
}

TEST(Lab2BinderTest, BindQualifiedResolvesAmbiguity) {
	auto catalog = MakeCatalog();
	auto bound = BindSql(*catalog,
	                     "SELECT orders.o_custkey FROM orders JOIN customer "
	                     "ON orders.o_custkey = customer.o_custkey");
	EXPECT_EQ(bound->names.size(), 1);
	// plan: projection over join
	ASSERT_EQ(bound->plan->type, LogicalOperatorType::LOGICAL_PROJECTION);
	ASSERT_EQ(bound->plan->children[0]->type, LogicalOperatorType::LOGICAL_JOIN);
	auto &join = static_cast<LogicalJoin &>(*bound->plan->children[0]);
	EXPECT_EQ(join.conditions.size(), 1);
}

TEST(Lab2BinderTest, BindAggregateRewrite) {
	auto catalog = MakeCatalog();
	auto bound = BindSql(*catalog, "SELECT l_returnflag, count(*) FROM lineitem GROUP BY l_returnflag");
	// plan: projection over aggregate over get
	ASSERT_EQ(bound->plan->type, LogicalOperatorType::LOGICAL_PROJECTION);
	ASSERT_EQ(bound->plan->children[0]->type, LogicalOperatorType::LOGICAL_AGGREGATE);
	auto &aggregate = static_cast<LogicalAggregate &>(*bound->plan->children[0]);
	EXPECT_EQ(aggregate.groups.size(), 1);
	EXPECT_EQ(aggregate.aggregates.size(), 1);
	EXPECT_EQ(aggregate.aggregates[0]->type, ExpressionType::AGGREGATE_COUNT_STAR);
	EXPECT_EQ(bound->types[1].Id(), LogicalTypeId::BIGINT);
}

TEST(Lab2BinderTest, BindAggregateTypes) {
	auto catalog = MakeCatalog();
	auto bound = BindSql(*catalog,
	                     "SELECT sum(l_quantity), avg(l_quantity), min(l_orderkey), max(l_returnflag) FROM lineitem");
	EXPECT_EQ(bound->types[0].Id(), LogicalTypeId::DOUBLE);
	EXPECT_EQ(bound->types[1].Id(), LogicalTypeId::DOUBLE);
	EXPECT_EQ(bound->types[2].Id(), LogicalTypeId::INTEGER);
	EXPECT_EQ(bound->types[3].Id(), LogicalTypeId::VARCHAR);
}

TEST(Lab2BinderTest, BindWhereProducesFilter) {
	auto catalog = MakeCatalog();
	auto bound = BindSql(*catalog, "SELECT l_orderkey FROM lineitem WHERE l_quantity > 10");
	ASSERT_EQ(bound->plan->type, LogicalOperatorType::LOGICAL_PROJECTION);
	ASSERT_EQ(bound->plan->children[0]->type, LogicalOperatorType::LOGICAL_FILTER);
	ASSERT_EQ(bound->plan->children[0]->children[0]->type, LogicalOperatorType::LOGICAL_GET);
}

TEST(Lab2BinderTest, BindOrderAndLimit) {
	auto catalog = MakeCatalog();
	auto bound = BindSql(*catalog, "SELECT l_orderkey FROM lineitem ORDER BY l_orderkey DESC LIMIT 3");
	ASSERT_EQ(bound->plan->type, LogicalOperatorType::LOGICAL_LIMIT);
	ASSERT_EQ(bound->plan->children[0]->type, LogicalOperatorType::LOGICAL_ORDER);
	auto &order = static_cast<LogicalOrder &>(*bound->plan->children[0]);
	EXPECT_EQ(order.keys.size(), 1);
	EXPECT_FALSE(order.keys[0].second);
}

TEST(Lab2BinderTest, BindInsertCoercesTypes) {
	auto catalog = MakeCatalog();
	// integer literal into a DOUBLE column is fine
	auto bound = BindSql(*catalog, "INSERT INTO lineitem VALUES (1, 2, 'A')");
	ASSERT_EQ(bound->type, StatementType::INSERT_STATEMENT);
	EXPECT_EQ(bound->rows.size(), 1);
	EXPECT_EQ(bound->rows[0][1].GetType().Id(), LogicalTypeId::DOUBLE);
	// varchar into an INTEGER column is not
	EXPECT_THROW(BindSql(*catalog, "INSERT INTO lineitem VALUES ('x', 2.0, 'A')"), BinderException);
}
