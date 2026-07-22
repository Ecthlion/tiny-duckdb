#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/common/value.hpp"
#include "tiny_duckdb/common/vector.hpp"

namespace tiny_duckdb {

//! A collection of equally-sized vectors: the unit of data flow in the execution engine.
class DataChunk {
public:
	DataChunk() = default;

	//! Allocate one vector per type
	void Initialize(const std::vector<LogicalType> &types);

	idx_t size() const;
	void SetCardinality(idx_t count);
	idx_t ColumnCount() const;

	Vector &GetVector(idx_t column);
	const Vector &GetVector(idx_t column) const;
	const std::vector<LogicalType> &GetTypes() const;

	Value GetValue(idx_t column, idx_t row) const;
	void SetValue(idx_t column, idx_t row, const Value &value);

	//! Keep only the rows referenced by the selection vector (compaction)
	void Slice(const SelectionVector &sel, idx_t count);

	//! Append a single row of values (used by the INSERT path and by tests)
	void AppendRow(const std::vector<Value> &row);

	//! Deep-copy another chunk into this one (used when materializing data)
	void CopyFrom(const DataChunk &other);

	void Reset();

	//! Pretty-print as an ASCII table (debug helper)
	std::string ToString() const;

private:
	std::vector<std::unique_ptr<Vector>> data_;
	std::vector<LogicalType> types_;
	idx_t count_ = 0;
};

} // namespace tiny_duckdb
