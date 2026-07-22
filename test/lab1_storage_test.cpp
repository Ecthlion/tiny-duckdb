#include <thread>
#include <vector>

#include "tdbtest.h"
#include "tiny_duckdb/storage/table_data.hpp"

using namespace tiny_duckdb;

// ---------------------------------------------------------------------------
// LAB 1 - columnar storage
// ---------------------------------------------------------------------------

namespace {

Vector MakeIntVector(idx_t start, idx_t count) {
	Vector result(LogicalType::Integer());
	for (idx_t i = 0; i < count; i++) {
		result.SetValue(i, Value::Integer(static_cast<int32_t>(start + i)));
	}
	return result;
}

std::unique_ptr<TableData> MakeTable(idx_t rows) {
	auto table = std::make_unique<TableData>("t", std::vector<std::string> {"a", "b"},
	                                    std::vector<LogicalType> {LogicalType::Integer(), LogicalType::Varchar()});
	DataChunk chunk;
	chunk.Initialize({LogicalType::Integer(), LogicalType::Varchar()});
	for (idx_t i = 0; i < rows; i++) {
		chunk.AppendRow({Value::Integer(static_cast<int32_t>(i)), Value::Varchar("str_" + std::to_string(i))});
		if (chunk.size() == STANDARD_VECTOR_SIZE) {
			table->Append(chunk);
			chunk.Reset();
		}
	}
	if (chunk.size() > 0) {
		table->Append(chunk);
	}
	return table;
}

} // namespace

TEST(Lab1StorageTest, ColumnChunkSingleBlock) {
	ColumnChunk chunk(LogicalType::Integer());
	auto data = MakeIntVector(0, 100);
	chunk.Append(data, 0, 100);
	EXPECT_EQ(chunk.Count(), 100);
	Vector out(LogicalType::Integer());
	chunk.Scan(0, 100, out, 0);
	for (idx_t i = 0; i < 100; i++) {
		EXPECT_EQ(out.GetValue(i), Value::Integer(static_cast<int32_t>(i)));
	}
}

TEST(Lab1StorageTest, ColumnChunkAcrossBlocks) {
	ColumnChunk chunk(LogicalType::Integer());
	const idx_t total = STANDARD_VECTOR_SIZE * 2 + 500;
	auto data = MakeIntVector(0, STANDARD_VECTOR_SIZE);
	chunk.Append(data, 0, STANDARD_VECTOR_SIZE);
	chunk.Append(data, 0, STANDARD_VECTOR_SIZE);
	auto tail = MakeIntVector(0, 500);
	chunk.Append(tail, 0, 500);
	EXPECT_EQ(chunk.Count(), total);
	Vector out(LogicalType::Integer());
	chunk.Scan(STANDARD_VECTOR_SIZE - 10, 20, out, 0);
	for (idx_t i = 0; i < 20; i++) {
		// rows STANDARD_VECTOR_SIZE-10 .. +10 cross the block boundary
		int32_t expected = static_cast<int32_t>((STANDARD_VECTOR_SIZE - 10 + i) % STANDARD_VECTOR_SIZE);
		EXPECT_EQ(out.GetValue(i), Value::Integer(expected));
	}
}

TEST(Lab1StorageTest, ColumnChunkNulls) {
	ColumnChunk chunk(LogicalType::Integer());
	Vector data(LogicalType::Integer());
	for (idx_t i = 0; i < 10; i++) {
		if (i % 2 == 0) {
			data.SetValue(i, Value::Integer(static_cast<int32_t>(i)));
		} else {
			data.SetValue(i, Value::Null(LogicalType::Integer()));
		}
	}
	chunk.Append(data, 0, 10);
	Vector out(LogicalType::Integer());
	chunk.Scan(0, 10, out, 0);
	for (idx_t i = 0; i < 10; i++) {
		if (i % 2 == 0) {
			EXPECT_EQ(out.GetValue(i), Value::Integer(static_cast<int32_t>(i)));
		} else {
			EXPECT_TRUE(out.GetValue(i).IsNull());
		}
	}
}

TEST(Lab1StorageTest, ColumnChunkVarchar) {
	ColumnChunk chunk(LogicalType::Varchar());
	Vector data(LogicalType::Varchar());
	for (idx_t i = 0; i < 100; i++) {
		data.SetValue(i, Value::Varchar("value_" + std::to_string(i)));
	}
	chunk.Append(data, 0, 100);
	Vector out(LogicalType::Varchar());
	chunk.Scan(50, 10, out, 0);
	for (idx_t i = 0; i < 10; i++) {
		EXPECT_EQ(out.GetValue(i), Value::Varchar("value_" + std::to_string(50 + i)));
	}
}

TEST(Lab1StorageTest, ZoneMapMinMax) {
	ColumnChunk chunk(LogicalType::Integer());
	auto data = MakeIntVector(10, 90); // 10..99
	chunk.Append(data, 0, 90);
	EXPECT_TRUE(chunk.HasZoneMap());
	EXPECT_EQ(chunk.Min(), Value::Integer(10));
	EXPECT_EQ(chunk.Max(), Value::Integer(99));
}

TEST(Lab1StorageTest, ZoneMapPrunesImpossible) {
	ColumnChunk chunk(LogicalType::Integer());
	auto data = MakeIntVector(0, 100); // 0..99
	chunk.Append(data, 0, 100);
	// col > 100 cannot be true (max = 99)
	EXPECT_FALSE(chunk.CheckZoneMap(Value::Integer(100), ExpressionType::COMPARE_GREATER_THAN));
	// col < 0 cannot be true (min = 0)
	EXPECT_FALSE(chunk.CheckZoneMap(Value::Integer(0), ExpressionType::COMPARE_LESS_THAN));
	// col = 1000 cannot be true
	EXPECT_FALSE(chunk.CheckZoneMap(Value::Integer(1000), ExpressionType::COMPARE_EQUAL));
}

TEST(Lab1StorageTest, ZoneMapKeepsPossible) {
	ColumnChunk chunk(LogicalType::Integer());
	auto data = MakeIntVector(0, 100); // 0..99
	chunk.Append(data, 0, 100);
	EXPECT_TRUE(chunk.CheckZoneMap(Value::Integer(50), ExpressionType::COMPARE_GREATER_THAN));
	EXPECT_TRUE(chunk.CheckZoneMap(Value::Integer(50), ExpressionType::COMPARE_EQUAL));
	EXPECT_TRUE(chunk.CheckZoneMap(Value::Integer(0), ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL));
}

TEST(Lab1StorageTest, ZoneMapBoundaryInclusive) {
	ColumnChunk chunk(LogicalType::Integer());
	auto data = MakeIntVector(5, 10); // 5..14
	chunk.Append(data, 0, 10);
	// col >= 14 is possible (max = 14)
	EXPECT_TRUE(chunk.CheckZoneMap(Value::Integer(14), ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL));
	// col <= 5 is possible (min = 5)
	EXPECT_TRUE(chunk.CheckZoneMap(Value::Integer(5), ExpressionType::COMPARE_LESS_THAN_OR_EQUAL));
	// col = 4 / col = 15 impossible
	EXPECT_FALSE(chunk.CheckZoneMap(Value::Integer(4), ExpressionType::COMPARE_EQUAL));
	EXPECT_FALSE(chunk.CheckZoneMap(Value::Integer(15), ExpressionType::COMPARE_EQUAL));
}

TEST(Lab1StorageTest, RowGroupAppendScan) {
	RowGroup group({LogicalType::Integer(), LogicalType::Varchar()});
	DataChunk chunk;
	chunk.Initialize({LogicalType::Integer(), LogicalType::Varchar()});
	for (idx_t i = 0; i < 1000; i++) {
		chunk.AppendRow({Value::Integer(static_cast<int32_t>(i * 2)), Value::Varchar("s" + std::to_string(i))});
	}
	group.Append(chunk);
	EXPECT_EQ(group.Count(), 1000);
	DataChunk out;
	out.Initialize({LogicalType::Varchar(), LogicalType::Integer()});
	// scan with reordered column list
	group.Scan(100, 5, {1, 0}, out);
	for (idx_t i = 0; i < 5; i++) {
		EXPECT_EQ(out.GetValue(0, i), Value::Varchar("s" + std::to_string(100 + i)));
		EXPECT_EQ(out.GetValue(1, i), Value::Integer(static_cast<int32_t>((100 + i) * 2)));
	}
}

TEST(Lab1StorageTest, TableDataSplitsRowGroups) {
	const idx_t rows = ROW_GROUP_SIZE * 2 + 17;
	auto table = MakeTable(rows);
	EXPECT_EQ(table->RowCount(), rows);
	EXPECT_EQ(table->RowGroupCount(), 3);
}

TEST(Lab1StorageTest, TableScanMorselsCoverAllRows) {
	const idx_t rows = ROW_GROUP_SIZE + 1234;
	auto table = MakeTable(rows);
	auto state = table->CreateParallelScanState();
	std::vector<TableScanMorsel> morsels;
	TableScanMorsel morsel;
	while (state->NextMorsel(morsel)) {
		morsels.push_back(morsel);
	}
	// morsels are STANDARD_VECTOR_SIZE slices of row groups
	idx_t covered = 0;
	for (const auto &m : morsels) {
		EXPECT_TRUE(m.count > 0);
		EXPECT_TRUE(m.count <= STANDARD_VECTOR_SIZE);
		covered += m.count;
	}
	EXPECT_EQ(covered, rows);
}

TEST(Lab1StorageTest, TableScanRoundTrip) {
	auto table = MakeTable(100);
	TableScanMorsel morsel {0, 10, 20};
	DataChunk out;
	out.Initialize({LogicalType::Integer(), LogicalType::Varchar()});
	table->Scan(morsel, {0, 1}, out);
	EXPECT_EQ(out.size(), 20);
	for (idx_t i = 0; i < 20; i++) {
		EXPECT_EQ(out.GetValue(0, i), Value::Integer(static_cast<int32_t>(10 + i)));
		EXPECT_EQ(out.GetValue(1, i), Value::Varchar("str_" + std::to_string(10 + i)));
	}
}
