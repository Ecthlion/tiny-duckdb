#include "tiny_duckdb/storage/column_chunk.hpp"

#include <algorithm>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

ColumnChunk::ColumnChunk(const LogicalType &type) : type_(type) {
}

const LogicalType &ColumnChunk::GetType() const {
	return type_;
}

idx_t ColumnChunk::Count() const {
	return count_;
}

void ColumnChunk::Append(Vector &data, idx_t source_offset, idx_t count) {
	if (data.GetType() != type_) {
		throw StorageException("ColumnChunk::Append type mismatch");
	}
	// TODO(L1.T1): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L1.T1 not implemented yet");
}

void ColumnChunk::Scan(idx_t offset, idx_t count, Vector &out, idx_t out_offset) const {
	if (offset + count > count_) {
		throw StorageException("ColumnChunk::Scan out of range");
	}
	// TODO(L1.T2): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L1.T2 not implemented yet");
}

bool ColumnChunk::HasZoneMap() const {
	return has_zone_map_;
}

const Value &ColumnChunk::Min() const {
	return min_;
}

const Value &ColumnChunk::Max() const {
	return max_;
}

void ColumnChunk::UpdateZoneMap(const Value &value) {
	// TODO(L1.T3): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L1.T3 not implemented yet");
}

bool ColumnChunk::CheckZoneMap(const Value &constant, ExpressionType comparison) const {
	// TODO(L1.T3): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L1.T3 not implemented yet");
}

} // namespace tiny_duckdb
