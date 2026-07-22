#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/storage/row_group.hpp"

namespace tiny_duckdb {

//! A morsel handed out by the parallel table scan: a vector-sized slice of a
//! row group.
struct TableScanMorsel {
	idx_t row_group_index = 0;
	idx_t offset = 0;
	idx_t count = 0;
};

//! Shared scan state for morsel-driven parallelism. Worker threads call
//! NextMorsel() until the table is exhausted. (Provided for Lab 3; Lab 0's
//! MorselQueue is the primitive this builds on.)
class ParallelTableScanState {
public:
	//! Build the morsel list for the given row group counts
	void Initialize(const std::vector<idx_t> &row_group_counts);

	//! Thread-safe morsel dispenser. Returns false when the scan is done.
	bool NextMorsel(TableScanMorsel &morsel);

private:
	std::vector<TableScanMorsel> morsels_;
	std::atomic<idx_t> next_ {0};
};

//! ============================================================================
//! LAB 1 - Columnar storage
//!
//! A table: a schema plus a list of row groups.
//!
//! Task L1.T5: TableData::Append - split incoming chunks across row groups,
//!             creating new row groups when the current one is full.
//! ============================================================================
class TableData {
public:
	TableData(std::string name, std::vector<std::string> column_names, std::vector<LogicalType> column_types);

	const std::string &GetName() const;
	const std::vector<std::string> &GetColumnNames() const;
	const std::vector<LogicalType> &GetColumnTypes() const;
	idx_t ColumnCount() const;
	idx_t RowCount() const;
	idx_t RowGroupCount() const;

	//! Append an entire DataChunk (thread-safe)
	void Append(DataChunk &chunk);

	//! Read a morsel of data. column_ids refers to table column indexes.
	void Scan(const TableScanMorsel &morsel, const std::vector<idx_t> &column_ids, DataChunk &out) const;

	//! Zone map check on a row group (used by the scan to skip morsels)
	bool CheckZoneMap(idx_t row_group_index, idx_t column_id, const Value &constant,
	                  ExpressionType comparison) const;

	//! Fill a ParallelTableScanState with the current morsel layout
	std::unique_ptr<ParallelTableScanState> CreateParallelScanState() const;

private:
	std::string name_;
	std::vector<std::string> column_names_;
	std::vector<LogicalType> column_types_;

	mutable std::mutex lock_;
	std::vector<std::unique_ptr<RowGroup>> row_groups_;
	idx_t row_count_ = 0;
};

} // namespace tiny_duckdb
