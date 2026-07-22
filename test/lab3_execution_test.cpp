#include <algorithm>

#include "tdbtest.h"
#include "tiny_duckdb/execution/expression_executor.hpp"
#include "tiny_duckdb/main/tiny_duckdb.hpp"

using namespace tiny_duckdb;

// ---------------------------------------------------------------------------
// LAB 3 - vectorized, morsel-driven push execution (end-to-end)
// ---------------------------------------------------------------------------

namespace {

void Exec(TinyDuckDB &db, const std::string &sql) {
	Connection connection(db);
	connection.Query(sql);
}

std::unique_ptr<QueryResult> Run(TinyDuckDB &db, const std::string &sql) {
	Connection connection(db);
	return connection.Query(sql);
}

void SortRows(std::vector<std::vector<Value>> &rows) {
	std::sort(rows.begin(), rows.end(), [](const std::vector<Value> &left, const std::vector<Value> &right) {
		for (idx_t i = 0; i < left.size() && i < right.size(); i++) {
			if (Value::Equals(left[i], right[i])) {
				continue;
			}
			return Value::LessThan(left[i], right[i]);
		}
		return false;
	});
}

void ExpectRows(QueryResult &result, std::vector<std::vector<Value>> expected, bool sorted = false) {
	auto rows = result.ToRows();
	if (sorted) {
		SortRows(rows);
		SortRows(expected);
	}
	ASSERT_EQ(rows.size(), expected.size());
	for (idx_t i = 0; i < rows.size(); i++) {
		ASSERT_EQ(rows[i].size(), expected[i].size());
		for (idx_t j = 0; j < rows[i].size(); j++) {
			EXPECT_EQ(rows[i][j], expected[i][j]);
		}
	}
}

//! lineitem(l_orderkey INT, l_quantity DOUBLE, l_returnflag VARCHAR) with
//! `rows` rows; values are deterministic functions of the row index.
void CreateLineitem(TinyDuckDB &db, idx_t rows) {
	Exec(db, "CREATE TABLE lineitem (l_orderkey INTEGER, l_quantity DOUBLE, l_returnflag VARCHAR)");
	auto &table = db.GetCatalog().GetTable("lineitem");
	DataChunk chunk;
	chunk.Initialize(table.GetColumnTypes());
	for (idx_t i = 0; i < rows; i++) {
		chunk.AppendRow({Value::Integer(static_cast<int32_t>(i % 1000)),
		                 Value::Double(static_cast<double>(i % 50) + 0.5),
		                 Value::Varchar(std::string(1, static_cast<char>('A' + (i % 3))))});
		if (chunk.size() == STANDARD_VECTOR_SIZE) {
			table.Append(chunk);
			chunk.Reset();
		}
	}
	if (chunk.size() > 0) {
		table.Append(chunk);
	}
}

//! orders(o_orderkey, o_custkey), customer(o_custkey, c_name)
void CreateOrdersAndCustomer(TinyDuckDB &db) {
	Exec(db, "CREATE TABLE orders (o_orderkey INTEGER, o_custkey INTEGER)");
	Exec(db, "CREATE TABLE customer (c_custkey INTEGER, c_name VARCHAR)");
	Exec(db, "INSERT INTO customer VALUES (1, 'alice'), (2, 'bob')");
	Exec(db, "INSERT INTO orders VALUES (10, 1), (11, 1), (12, 2), (13, 3)");
}

} // namespace

// --- L3.T1: the expression executor ----------------------------------------

TEST(Lab3ExecutionTest, ExpressionEvaluatorArithmetic) {
	DataChunk chunk;
	chunk.Initialize({LogicalType::Integer()});
	for (idx_t i = 0; i < 10; i++) {
		chunk.AppendRow({Value::Integer(static_cast<int32_t>(i))});
	}
	// col * 2 + 1
	auto mul = std::make_unique<BoundOperatorExpression>(
	    ExpressionType::OPERATOR_MULTIPLY, std::make_unique<BoundColumnRefExpression>("a", 0, LogicalType::Integer()),
	    std::make_unique<BoundConstantExpression>(Value::Integer(2)), LogicalType::Integer());
	auto expr = std::make_unique<BoundOperatorExpression>(ExpressionType::OPERATOR_ADD, std::move(mul),
	                                                 std::make_unique<BoundConstantExpression>(Value::Integer(1)),
	                                                 LogicalType::Integer());
	Vector result(LogicalType::Integer());
	ExpressionExecutor::Evaluate(*expr, chunk, result);
	for (idx_t i = 0; i < 10; i++) {
		EXPECT_EQ(result.GetValue(i), Value::Integer(static_cast<int32_t>(i * 2 + 1)));
	}
}

TEST(Lab3ExecutionTest, ExpressionEvaluatorSelectWithNulls) {
	DataChunk chunk;
	chunk.Initialize({LogicalType::Integer()});
	for (idx_t i = 0; i < 10; i++) {
		if (i % 3 == 0) {
			chunk.AppendRow({Value::Null(LogicalType::Integer())});
		} else {
			chunk.AppendRow({Value::Integer(static_cast<int32_t>(i))});
		}
	}
	// col > 3  (NULL never matches)
	auto predicate = std::make_unique<BoundComparisonExpression>(
	    ExpressionType::COMPARE_GREATER_THAN,
	    std::make_unique<BoundColumnRefExpression>("a", 0, LogicalType::Integer()),
	    std::make_unique<BoundConstantExpression>(Value::Integer(3)));
	SelectionVector sel;
	idx_t matches = ExpressionExecutor::Select(*predicate, chunk, sel);
	// matching rows: 4, 5, 7, 8
	EXPECT_EQ(matches, 4);
	EXPECT_EQ(sel.get_index(0), 4);
	EXPECT_EQ(sel.get_index(1), 5);
	EXPECT_EQ(sel.get_index(2), 7);
	EXPECT_EQ(sel.get_index(3), 8);
}

// --- L3.T2: scan + filter + projection --------------------------------------

TEST(Lab3ExecutionTest, SelectStar) {
	TinyDuckDB db;
	CreateLineitem(db, 100);
	auto result = Run(db, "SELECT * FROM lineitem");
	EXPECT_EQ(result->RowCount(), 100);
	EXPECT_EQ(result->Names().size(), 3);
}

TEST(Lab3ExecutionTest, ProjectionArithmetic) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER, b INTEGER)");
	Exec(db, "INSERT INTO t VALUES (1, 10), (2, 20)");
	auto result = Run(db, "SELECT a + b, a * b FROM t");
	ExpectRows(*result, {{Value::Integer(11), Value::Integer(10)}, {Value::Integer(22), Value::Integer(40)}});
}

TEST(Lab3ExecutionTest, WhereFilter) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER, b VARCHAR)");
	Exec(db, "INSERT INTO t VALUES (1, 'x'), (5, 'y'), (9, 'z')");
	auto result = Run(db, "SELECT b FROM t WHERE a > 4");
	ExpectRows(*result, {{Value::Varchar("y")}, {Value::Varchar("z")}});
}

TEST(Lab3ExecutionTest, WhereConjunction) {
	TinyDuckDB db;
	CreateLineitem(db, 1000);
	auto result = Run(db, "SELECT l_orderkey FROM lineitem WHERE l_returnflag = 'B' AND l_quantity >= 10");
	for (const auto &row : result->ToRows()) {
		EXPECT_EQ(row[0].GetInteger() % 3, 1); // 'B' rows
	}
	// every third row is 'B'; of those, quantity = (i % 50) + 0.5 >= 10
	auto all = Run(db, "SELECT count(*) FROM lineitem WHERE l_returnflag = 'B' AND l_quantity >= 10");
	EXPECT_TRUE(all->GetValue(0, 0).GetBigInt() > 0);
	EXPECT_EQ(all->GetValue(0, 0), result->ToRows().empty() ? Value::BigInt(0)
	                                                        : Value::BigInt(static_cast<int64_t>(result->RowCount())));
}

// --- L3.T3: morsel-driven scan + zone map pruning ---------------------------

TEST(Lab3ExecutionTest, ZoneMapPrunedScanStillCorrect) {
	TinyDuckDB db;
	// multiple row groups; the predicate prunes most of them
	CreateLineitem(db, ROW_GROUP_SIZE * 3 + 100);
	auto result = Run(db, "SELECT count(*) FROM lineitem WHERE l_orderkey < 10");
	idx_t expected = 0;
	for (idx_t i = 0; i < ROW_GROUP_SIZE * 3 + 100; i++) {
		if (i % 1000 < 10) {
			expected++;
		}
	}
	EXPECT_EQ(result->GetValue(0, 0), Value::BigInt(static_cast<int64_t>(expected)));
	// predicate that prunes EVERYTHING
	auto empty = Run(db, "SELECT count(*) FROM lineitem WHERE l_orderkey > 100000");
	EXPECT_EQ(empty->GetValue(0, 0), Value::BigInt(0));
}

TEST(Lab3ExecutionTest, ParallelScanConsistency) {
	TinyDuckDB db;
	CreateLineitem(db, ROW_GROUP_SIZE * 2 + 777);
	db.SetThreads(1);
	auto single = Run(db, "SELECT count(*), sum(l_quantity) FROM lineitem");
	db.SetThreads(4);
	auto multi = Run(db, "SELECT count(*), sum(l_quantity) FROM lineitem");
	EXPECT_EQ(single->GetValue(0, 0), multi->GetValue(0, 0));
	EXPECT_EQ(single->GetValue(1, 0), multi->GetValue(1, 0));
}

// --- L3.T4: the parallel hash aggregate --------------------------------------

TEST(Lab3ExecutionTest, CountStar) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER)");
	Exec(db, "INSERT INTO t VALUES (1), (2), (3)");
	auto result = Run(db, "SELECT count(*) FROM t");
	ExpectRows(*result, {{Value::BigInt(3)}});
}

TEST(Lab3ExecutionTest, AggregateEmptyTableNoGroupBy) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER)");
	// aggregation without GROUP BY always emits exactly one row
	auto result = Run(db, "SELECT count(*), sum(a), avg(a), min(a) FROM t");
	ASSERT_EQ(result->RowCount(), 1);
	EXPECT_EQ(result->GetValue(0, 0), Value::BigInt(0));
	EXPECT_TRUE(result->GetValue(1, 0).IsNull());
	EXPECT_TRUE(result->GetValue(2, 0).IsNull());
	EXPECT_TRUE(result->GetValue(3, 0).IsNull());
}

TEST(Lab3ExecutionTest, SumAvgMinMax) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER)");
	Exec(db, "INSERT INTO t VALUES (10), (20), (30)");
	auto result = Run(db, "SELECT sum(a), min(a), max(a) FROM t");
	ExpectRows(*result, {{Value::BigInt(60), Value::Integer(10), Value::Integer(30)}});
	auto avg = Run(db, "SELECT avg(a) FROM t");
	EXPECT_NEAR(avg->GetValue(0, 0).GetDouble(), 20.0, 1e-9);
}

TEST(Lab3ExecutionTest, GroupBySingleColumn) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (g VARCHAR, v INTEGER)");
	Exec(db, "INSERT INTO t VALUES ('a', 1), ('b', 2), ('a', 3), ('b', 4), ('a', 5)");
	auto result = Run(db, "SELECT g, count(*), sum(v) FROM t GROUP BY g");
	ExpectRows(*result,
	           {{Value::Varchar("a"), Value::BigInt(3), Value::BigInt(9)},
	            {Value::Varchar("b"), Value::BigInt(2), Value::BigInt(6)}},
	           true);
}

TEST(Lab3ExecutionTest, GroupByEmptyTable) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (g VARCHAR, v INTEGER)");
	auto result = Run(db, "SELECT g, count(*) FROM t GROUP BY g");
	EXPECT_EQ(result->RowCount(), 0);
}

TEST(Lab3ExecutionTest, ParallelGroupByConsistency) {
	TinyDuckDB db;
	CreateLineitem(db, ROW_GROUP_SIZE * 2 + 123);
	db.SetThreads(1);
	auto single = Run(db, "SELECT l_returnflag, count(*), sum(l_quantity) FROM lineitem GROUP BY l_returnflag");
	db.SetThreads(4);
	auto multi = Run(db, "SELECT l_returnflag, count(*), sum(l_quantity) FROM lineitem GROUP BY l_returnflag");
	auto single_rows = single->ToRows();
	auto multi_rows = multi->ToRows();
	SortRows(single_rows);
	SortRows(multi_rows);
	ASSERT_EQ(single_rows.size(), 3);
	ASSERT_EQ(multi_rows.size(), 3);
	for (idx_t i = 0; i < 3; i++) {
		EXPECT_EQ(single_rows[i][0], multi_rows[i][0]);
		EXPECT_EQ(single_rows[i][1], multi_rows[i][1]);
		EXPECT_EQ(single_rows[i][2], multi_rows[i][2]);
	}
}

// --- L3.T5: the hash join ----------------------------------------------------

TEST(Lab3ExecutionTest, JoinSimple) {
	TinyDuckDB db;
	CreateOrdersAndCustomer(db);
	auto result = Run(db, "SELECT c_name, o_orderkey FROM orders JOIN customer ON o_custkey = c_custkey");
	ExpectRows(*result,
	           {{Value::Varchar("alice"), Value::Integer(10)},
	            {Value::Varchar("alice"), Value::Integer(11)},
	            {Value::Varchar("bob"), Value::Integer(12)}},
	           true);
}

TEST(Lab3ExecutionTest, JoinWithFilter) {
	TinyDuckDB db;
	CreateOrdersAndCustomer(db);
	auto result =
	    Run(db, "SELECT o_orderkey FROM orders JOIN customer ON o_custkey = c_custkey WHERE c_name = 'alice'");
	ExpectRows(*result, {{Value::Integer(10)}, {Value::Integer(11)}}, true);
}

TEST(Lab3ExecutionTest, JoinGroupBy) {
	TinyDuckDB db;
	CreateOrdersAndCustomer(db);
	auto result = Run(
	    db, "SELECT c_name, count(*) FROM orders JOIN customer ON o_custkey = c_custkey GROUP BY c_name");
	ExpectRows(*result,
	           {{Value::Varchar("alice"), Value::BigInt(2)}, {Value::Varchar("bob"), Value::BigInt(1)}}, true);
}

TEST(Lab3ExecutionTest, JoinLargeParallel) {
	TinyDuckDB db;
	db.SetThreads(4);
	CreateLineitem(db, ROW_GROUP_SIZE + 500);
	Exec(db, "CREATE TABLE keys (k INTEGER)");
	Exec(db, "INSERT INTO keys VALUES (1), (2), (3)");
	auto result = Run(db,
	                  "SELECT count(*) FROM lineitem JOIN keys ON l_orderkey = k");
	// rows with l_orderkey in {1,2,3}
	idx_t expected = 0;
	for (idx_t i = 0; i < ROW_GROUP_SIZE + 500; i++) {
		if (i % 1000 >= 1 && i % 1000 <= 3) {
			expected++;
		}
	}
	EXPECT_EQ(result->GetValue(0, 0), Value::BigInt(static_cast<int64_t>(expected)));
}

// --- L3.T6: ORDER BY / LIMIT -------------------------------------------------

TEST(Lab3ExecutionTest, OrderByAsc) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER)");
	Exec(db, "INSERT INTO t VALUES (3), (1), (2), (5), (4)");
	auto result = Run(db, "SELECT a FROM t ORDER BY a");
	ExpectRows(*result, {{Value::Integer(1)}, {Value::Integer(2)}, {Value::Integer(3)}, {Value::Integer(4)},
	                     {Value::Integer(5)}});
}

TEST(Lab3ExecutionTest, OrderByDescLimit) {
	TinyDuckDB db;
	CreateLineitem(db, 100);
	auto result = Run(db, "SELECT l_orderkey FROM lineitem ORDER BY l_orderkey DESC LIMIT 3");
	ExpectRows(*result, {{Value::Integer(99)}, {Value::Integer(98)}, {Value::Integer(97)}});
}

TEST(Lab3ExecutionTest, LimitOnly) {
	TinyDuckDB db;
	CreateLineitem(db, 10000);
	auto result = Run(db, "SELECT * FROM lineitem LIMIT 7");
	EXPECT_EQ(result->RowCount(), 7);
}

TEST(Lab3ExecutionTest, OrderByGroupByResult) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (g VARCHAR, v INTEGER)");
	Exec(db, "INSERT INTO t VALUES ('b', 1), ('a', 2), ('c', 3), ('a', 4)");
	auto result = Run(db, "SELECT g, sum(v) FROM t GROUP BY g ORDER BY g");
	ExpectRows(*result,
	           {{Value::Varchar("a"), Value::BigInt(6)},
	            {Value::Varchar("b"), Value::BigInt(1)},
	            {Value::Varchar("c"), Value::BigInt(3)}});
}

// --- extra coverage, one block per task -------------------------------------

TEST(Lab3ExecutionTest, ExpressionEvaluatorConstant) {
	DataChunk chunk;
	chunk.Initialize({LogicalType::Integer()});
	chunk.AppendRow({Value::Integer(1)});
	chunk.AppendRow({Value::Integer(2)});
	BoundConstantExpression expr(Value::Varchar("const"));
	Vector result(LogicalType::Varchar());
	ExpressionExecutor::Evaluate(expr, chunk, result);
	// a constant is broadcast to every row
	EXPECT_EQ(result.GetValue(0), Value::Varchar("const"));
	EXPECT_EQ(result.GetValue(1), Value::Varchar("const"));
}

TEST(Lab3ExecutionTest, ExpressionEvaluatorVarcharComparison) {
	DataChunk chunk;
	chunk.Initialize({LogicalType::Varchar()});
	chunk.AppendRow({Value::Varchar("apple")});
	chunk.AppendRow({Value::Varchar("banana")});
	chunk.AppendRow({Value::Varchar("cherry")});
	// col < 'c' matches "apple", "banana"
	auto predicate = std::make_unique<BoundComparisonExpression>(
	    ExpressionType::COMPARE_LESS_THAN,
	    std::make_unique<BoundColumnRefExpression>("s", 0, LogicalType::Varchar()),
	    std::make_unique<BoundConstantExpression>(Value::Varchar("c")));
	SelectionVector sel;
	idx_t matches = ExpressionExecutor::Select(*predicate, chunk, sel);
	EXPECT_EQ(matches, 2);
	EXPECT_EQ(sel.get_index(0), 0);
	EXPECT_EQ(sel.get_index(1), 1);
}

TEST(Lab3ExecutionTest, ExpressionEvaluatorConjunction) {
	DataChunk chunk;
	chunk.Initialize({LogicalType::Integer()});
	for (idx_t i = 0; i < 10; i++) {
		chunk.AppendRow({Value::Integer(static_cast<int32_t>(i))});
	}
	auto gt = std::make_unique<BoundComparisonExpression>(
	    ExpressionType::COMPARE_GREATER_THAN,
	    std::make_unique<BoundColumnRefExpression>("a", 0, LogicalType::Integer()),
	    std::make_unique<BoundConstantExpression>(Value::Integer(2)));
	auto lt = std::make_unique<BoundComparisonExpression>(
	    ExpressionType::COMPARE_LESS_THAN,
	    std::make_unique<BoundColumnRefExpression>("a", 0, LogicalType::Integer()),
	    std::make_unique<BoundConstantExpression>(Value::Integer(6)));
	// a > 2 AND a < 6 -> rows 3, 4, 5
	auto conjunction = std::make_unique<BoundConjunctionExpression>(ExpressionType::CONJUNCTION_AND, std::move(gt),
	                                                           std::move(lt));
	SelectionVector sel;
	EXPECT_EQ(ExpressionExecutor::Select(*conjunction, chunk, sel), 3);
	// a > 2 OR a < 6 -> every row
	auto disjunction = std::make_unique<BoundConjunctionExpression>(
	    ExpressionType::CONJUNCTION_OR,
	    std::make_unique<BoundComparisonExpression>(
	        ExpressionType::COMPARE_GREATER_THAN,
	        std::make_unique<BoundColumnRefExpression>("a", 0, LogicalType::Integer()),
	        std::make_unique<BoundConstantExpression>(Value::Integer(2))),
	    std::make_unique<BoundComparisonExpression>(
	        ExpressionType::COMPARE_LESS_THAN,
	        std::make_unique<BoundColumnRefExpression>("a", 0, LogicalType::Integer()),
	        std::make_unique<BoundConstantExpression>(Value::Integer(6))));
	SelectionVector sel2;
	EXPECT_EQ(ExpressionExecutor::Select(*disjunction, chunk, sel2), 10);
}

TEST(Lab3ExecutionTest, FilterNoRowsMatch) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER)");
	Exec(db, "INSERT INTO t VALUES (1), (2), (3)");
	auto result = Run(db, "SELECT * FROM t WHERE a > 100");
	EXPECT_EQ(result->RowCount(), 0);
}

TEST(Lab3ExecutionTest, ProjectionConstantColumns) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER)");
	Exec(db, "INSERT INTO t VALUES (1), (2)");
	auto result = Run(db, "SELECT 42, 'x', a FROM t");
	ExpectRows(*result,
	           {{Value::Integer(42), Value::Varchar("x"), Value::Integer(1)},
	            {Value::Integer(42), Value::Varchar("x"), Value::Integer(2)}});
}

TEST(Lab3ExecutionTest, ScanEmptyTable) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER, b VARCHAR)");
	auto result = Run(db, "SELECT * FROM t");
	EXPECT_EQ(result->RowCount(), 0);
	auto filtered = Run(db, "SELECT * FROM t WHERE a > 0");
	EXPECT_EQ(filtered->RowCount(), 0);
}

TEST(Lab3ExecutionTest, AggregateCountColumnSkipsNull) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER)");
	Exec(db, "INSERT INTO t VALUES (1), (2), (NULL)");
	auto result = Run(db, "SELECT count(a), count(*), sum(a) FROM t");
	ExpectRows(*result, {{Value::BigInt(2), Value::BigInt(3), Value::BigInt(3)}});
}

TEST(Lab3ExecutionTest, AggregateMinMaxVarchar) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (s VARCHAR)");
	Exec(db, "INSERT INTO t VALUES ('banana'), ('apple'), ('cherry')");
	auto result = Run(db, "SELECT min(s), max(s) FROM t");
	ExpectRows(*result, {{Value::Varchar("apple"), Value::Varchar("cherry")}});
}

TEST(Lab3ExecutionTest, JoinDuplicateKeys) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE l (k INTEGER, v VARCHAR)");
	Exec(db, "CREATE TABLE r (k INTEGER, w VARCHAR)");
	// one probe row matches three build rows -> three output rows
	Exec(db, "INSERT INTO l VALUES (1, 'a')");
	Exec(db, "INSERT INTO r VALUES (1, 'x'), (1, 'y'), (1, 'z')");
	auto result = Run(db, "SELECT v, w FROM l JOIN r ON l.k = r.k");
	ExpectRows(*result,
	           {{Value::Varchar("a"), Value::Varchar("x")},
	            {Value::Varchar("a"), Value::Varchar("y")},
	            {Value::Varchar("a"), Value::Varchar("z")}},
	           true);
}

TEST(Lab3ExecutionTest, JoinFanoutBeyondVectorSize) {
	TinyDuckDB db;
	db.SetThreads(1); // deterministic single-thread drain of the probe state
	Exec(db, "CREATE TABLE big (k INTEGER)");
	Exec(db, "CREATE TABLE small (k INTEGER)");
	// 5000 build rows + 3 probe rows, all with key 1 -> 15000 output rows,
	// far beyond STANDARD_VECTOR_SIZE: the probe must resume across calls
	// (HAVE_MORE_OUTPUT). Regression test for lost pending output.
	auto &big = db.GetCatalog().GetTable("big");
	DataChunk chunk;
	chunk.Initialize({LogicalType::Integer()});
	for (idx_t i = 0; i < 5000; i++) {
		chunk.AppendRow({Value::Integer(1)});
		if (chunk.size() == STANDARD_VECTOR_SIZE) {
			big.Append(chunk);
			chunk.Reset();
		}
	}
	if (chunk.size() > 0) {
		big.Append(chunk);
	}
	Exec(db, "INSERT INTO small VALUES (1), (1), (1)");
	auto result = Run(db, "SELECT count(*) FROM small JOIN big ON small.k = big.k");
	EXPECT_EQ(result->GetValue(0, 0), Value::BigInt(15000));
}

TEST(Lab3ExecutionTest, JoinEmptyBuildSide) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE l (k INTEGER)");
	Exec(db, "CREATE TABLE r (k INTEGER)");
	Exec(db, "INSERT INTO l VALUES (1), (2)");
	auto result = Run(db, "SELECT count(*) FROM l JOIN r ON l.k = r.k");
	EXPECT_EQ(result->GetValue(0, 0), Value::BigInt(0));
}

TEST(Lab3ExecutionTest, OrderByMultipleKeys) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER, b INTEGER)");
	Exec(db, "INSERT INTO t VALUES (2, 1), (1, 2), (2, 0), (1, 1)");
	auto result = Run(db, "SELECT a, b FROM t ORDER BY a ASC, b DESC");
	ExpectRows(*result,
	           {{Value::Integer(1), Value::Integer(2)},
	            {Value::Integer(1), Value::Integer(1)},
	            {Value::Integer(2), Value::Integer(1)},
	            {Value::Integer(2), Value::Integer(0)}});
}

TEST(Lab3ExecutionTest, LimitZeroAndBeyondTotal) {
	TinyDuckDB db;
	Exec(db, "CREATE TABLE t (a INTEGER)");
	Exec(db, "INSERT INTO t VALUES (1), (2), (3)");
	auto zero = Run(db, "SELECT * FROM t LIMIT 0");
	EXPECT_EQ(zero->RowCount(), 0);
	auto beyond = Run(db, "SELECT * FROM t LIMIT 100");
	EXPECT_EQ(beyond->RowCount(), 3);
}
