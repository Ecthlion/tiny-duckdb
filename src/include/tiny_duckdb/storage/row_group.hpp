#pragma once

#include <memory>
#include <vector>

#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/storage/column_chunk.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 1 - Columnar storage: the RowGroup
//!
//! A RowGroup is a horizontal partition of a table (up to ROW_GROUP_SIZE
//! rows). Inside a row group, data is stored column-at-a-time: one
//! ColumnChunk per column. Row groups are the "morsels" of the parallel scan
//! in Lab 3.
//!
//! ----------------------------------------------------------------------------
//! Task L1.T4 - RowGroup::Append / RowGroup::Scan
//!
//! Route each column of a DataChunk to its ColumnChunk, and back:
//!   Append(chunk, source_offset, count) forwards every column i of the
//!     chunk to columns_[i]->Append(...); keep count_ in sync.
//!   Scan(offset, count, column_ids, out) reads count rows starting at row
//!     group offset `offset`, but ONLY the columns listed in column_ids:
//!     output column j comes from row-group column column_ids[j]. This is
//!     what lets a query read 2 columns out of a 16-column table without
//!     touching the other 14 (projection pushdown into storage).
//!
//! Hint: Scan assumes out was initialized with the column types in
//!       column_ids order - just forward to ColumnChunk::Scan per column.
//!
//! Tests: Lab1StorageTest.RowGroupAppendScan (includes a reordered column
//!        list), TableScanMorselsCoverAllRows / TableScanRoundTrip
//! ============================================================================
class RowGroup {
public:
	explicit RowGroup(const std::vector<LogicalType> &types);

	idx_t Count() const;
	idx_t CapacityLeft() const;

	//! Append count rows from chunk[source_offset ..] (all columns)
	void Append(DataChunk &chunk, idx_t source_offset, idx_t count);
	//! Append a whole chunk (must fit in the remaining capacity)
	void Append(DataChunk &chunk);
	//! Read count rows starting at row group offset `offset` for the given
	//! column ids into out (columns of out match column_ids order)
	void Scan(idx_t offset, idx_t count, const std::vector<idx_t> &column_ids, DataChunk &out) const;

	//! Zone map check for one column (see ColumnChunk::CheckZoneMap)
	bool CheckZoneMap(idx_t column_id, const Value &constant, ExpressionType comparison) const;

private:
	std::vector<std::unique_ptr<ColumnChunk>> columns_;
	idx_t count_ = 0;
};

} // namespace tiny_duckdb
