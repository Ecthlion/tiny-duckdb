#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/storage/row_group.hpp"

namespace tiny_duckdb {

//! A unit of parallel scan work: a STANDARD_VECTOR_SIZE slice of a row group
struct TableScanMorsel {
	idx_t row_group_index;
	idx_t offset;
	idx_t count;
};

//! ============================================================================
//! LAB 1 / LAB 3 - the table and its parallel scan state
//!
//! TableData = a list of RowGroups. Appends split chunks across row groups.
//!
//! ParallelTableScanState precomputes the morsels of a scan and hands them
//! out to worker threads exactly once (this is the morsel queue of Lab 0 in
//! action). Task L1.T5: TableData::Append.
//! ============================================================================
class ParallelTableScanState {
public:
	//! Precompute all morsels from per-row-group row counts
	void Initialize(const std::vector<idx_t> &row_group_counts);
	//! Grab the next morsel; returns false when the scan is done
	bool NextMorsel(TableScanMorsel &morsel);

private:
	std::vector<TableScanMorsel> morsels_;
	std::atomic<idx_t> next_ {0};
};

class TableData {
public:
	TableData(std::string name, std::vector<std::string> column_names, std::vector<LogicalType> column_types);

	const std::string &GetName() const;
	const std::vector<std::string> &GetColumnNames() const;
	const std::vector<LogicalType> &GetColumnTypes() const;
	idx_t ColumnCount() const;
	idx_t RowCount() const;
	idx_t RowGroupCount() const;

	//! Append a chunk of any size, splitting it across row groups
	void Append(DataChunk &chunk);

	//! Read one morsel (a slice of one row group) for the given column ids
	void Scan(const TableScanMorsel &morsel, const std::vector<idx_t> &column_ids, DataChunk &out) const;

	//! Zone map check for one column of one row group
	bool CheckZoneMap(idx_t row_group_index, idx_t column_id, const Value &constant,
	                  ExpressionType comparison) const;

	std::unique_ptr<ParallelTableScanState> CreateParallelScanState() const;

private:
	std::string name_;
	std::vector<std::string> column_names_;
	std::vector<LogicalType> column_types_;
	std::vector<std::unique_ptr<RowGroup>> row_groups_;
	idx_t row_count_ = 0;
	mutable std::mutex lock_;
};

} // namespace tiny_duckdb
