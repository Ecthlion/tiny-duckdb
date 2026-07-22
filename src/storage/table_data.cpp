#include "tiny_duckdb/storage/table_data.hpp"

#include <algorithm>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

void ParallelTableScanState::Initialize(const std::vector<idx_t> &row_group_counts) {
	morsels_.clear();
	for (idx_t rg = 0; rg < row_group_counts.size(); rg++) {
		idx_t offset = 0;
		while (offset < row_group_counts[rg]) {
			TableScanMorsel morsel;
			morsel.row_group_index = rg;
			morsel.offset = offset;
			morsel.count = std::min(STANDARD_VECTOR_SIZE, row_group_counts[rg] - offset);
			morsels_.push_back(morsel);
			offset += morsel.count;
		}
	}
	next_.store(0);
}

bool ParallelTableScanState::NextMorsel(TableScanMorsel &morsel) {
	const idx_t index = next_.fetch_add(1, std::memory_order_relaxed);
	if (index >= morsels_.size()) {
		return false;
	}
	morsel = morsels_[index];
	return true;
}

TableData::TableData(std::string name, std::vector<std::string> column_names,
                     std::vector<LogicalType> column_types)
    : name_(std::move(name)), column_names_(std::move(column_names)), column_types_(std::move(column_types)) {
	if (column_names_.size() != column_types_.size() || column_names_.empty()) {
		throw CatalogException("Table must have at least one column and matching names/types");
	}
}

const std::string &TableData::GetName() const {
	return name_;
}

const std::vector<std::string> &TableData::GetColumnNames() const {
	return column_names_;
}

const std::vector<LogicalType> &TableData::GetColumnTypes() const {
	return column_types_;
}

idx_t TableData::ColumnCount() const {
	return column_types_.size();
}

idx_t TableData::RowCount() const {
	return row_count_;
}

idx_t TableData::RowGroupCount() const {
	return row_groups_.size();
}

void TableData::Append(DataChunk &chunk) {
	if (chunk.ColumnCount() != ColumnCount()) {
		throw StorageException("TableData::Append column count mismatch");
	}
	for (idx_t col = 0; col < ColumnCount(); col++) {
		if (chunk.GetTypes()[col] != column_types_[col]) {
			throw StorageException("TableData::Append type mismatch on column " + column_names_[col]);
		}
	}
	std::lock_guard<std::mutex> guard(lock_);
	// [SOLUTION BEGIN L1.T5]
	idx_t offset = 0;
	while (offset < chunk.size()) {
		if (row_groups_.empty() || row_groups_.back()->CapacityLeft() == 0) {
			row_groups_.push_back(std::make_unique<RowGroup>(column_types_));
		}
		RowGroup &current = *row_groups_.back();
		const idx_t batch = std::min(current.CapacityLeft(), chunk.size() - offset);
		current.Append(chunk, offset, batch);
		offset += batch;
		row_count_ += batch;
	}
	// [SOLUTION END]
}

void TableData::Scan(const TableScanMorsel &morsel, const std::vector<idx_t> &column_ids, DataChunk &out) const {
	std::lock_guard<std::mutex> guard(lock_);
	if (morsel.row_group_index >= row_groups_.size()) {
		throw StorageException("TableData::Scan invalid row group");
	}
	row_groups_[morsel.row_group_index]->Scan(morsel.offset, morsel.count, column_ids, out);
}

bool TableData::CheckZoneMap(idx_t row_group_index, idx_t column_id, const Value &constant,
                             ExpressionType comparison) const {
	std::lock_guard<std::mutex> guard(lock_);
	if (row_group_index >= row_groups_.size()) {
		return true;
	}
	return row_groups_[row_group_index]->CheckZoneMap(column_id, constant, comparison);
}

std::unique_ptr<ParallelTableScanState> TableData::CreateParallelScanState() const {
	std::lock_guard<std::mutex> guard(lock_);
	std::vector<idx_t> counts;
	counts.reserve(row_groups_.size());
	for (const auto &row_group : row_groups_) {
		counts.push_back(row_group->Count());
	}
	auto state = std::make_unique<ParallelTableScanState>();
	state->Initialize(counts);
	return state;
}

} // namespace tiny_duckdb
