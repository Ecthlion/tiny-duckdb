#include "tiny_duckdb/storage/row_group.hpp"

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

RowGroup::RowGroup(const std::vector<LogicalType> &types) {
	for (const auto &type : types) {
		columns_.push_back(std::make_unique<ColumnChunk>(type));
	}
}

idx_t RowGroup::Count() const {
	return count_;
}

idx_t RowGroup::CapacityLeft() const {
	return ROW_GROUP_SIZE - count_;
}

void RowGroup::Append(DataChunk &chunk, idx_t source_offset, idx_t count) {
	if (count > CapacityLeft()) {
		throw StorageException("RowGroup::Append exceeds row group capacity");
	}
	// TODO(L1.T4): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L1.T4 not implemented yet");
}

void RowGroup::Append(DataChunk &chunk) {
	Append(chunk, 0, chunk.size());
}

void RowGroup::Scan(idx_t offset, idx_t count, const std::vector<idx_t> &column_ids, DataChunk &out) const {
	if (offset + count > count_) {
		throw StorageException("RowGroup::Scan out of range");
	}
	// TODO(L1.T4): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L1.T4 not implemented yet");
}

bool RowGroup::CheckZoneMap(idx_t column_id, const Value &constant, ExpressionType comparison) const {
	return columns_[column_id]->CheckZoneMap(constant, comparison);
}

} // namespace tiny_duckdb
