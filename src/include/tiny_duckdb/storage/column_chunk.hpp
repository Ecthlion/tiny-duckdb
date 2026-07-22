#pragma once

#include <memory>
#include <vector>

#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/common/value.hpp"
#include "tiny_duckdb/common/vector.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 1 - Columnar storage: the ColumnChunk
//!
//! A ColumnChunk stores ONE column of ONE row group, as a list of Vectors
//! (column-at-a-time layout, like DuckDB's column segments). It also
//! maintains a small zone map (min/max) that the scan can use to skip entire
//! row groups.
//!
//! ----------------------------------------------------------------------------
//! Task L1.T1 - ColumnChunk::Append
//!
//! Copy count values from data[source_offset .. source_offset + count) into
//! the chunk. The chunk is a list of block Vectors (each holding up to
//! STANDARD_VECTOR_SIZE values): fill the last block until it is full, then
//! allocate a new one and keep going. Watch out:
//!   * the source offset does NOT have to be 0, and the last block does NOT
//!     have to be empty - the general case copies across block boundaries;
//!   * NULLs and VARCHARs must round-trip exactly (Value handles both - use
//!     Vector::GetValue/SetValue and you get them for free).
//!
//! Hint: loop while count > 0, copying min(remaining, space_left_in_block)
//!       values per iteration.
//!
//! Tests: Lab1StorageTest.ColumnChunkSingleBlock / ColumnChunkAcrossBlocks /
//!        ColumnChunkNulls / ColumnChunkVarchar / ColumnChunkPartialAppends
//!
//! ----------------------------------------------------------------------------
//! Task L1.T2 - ColumnChunk::Scan
//!
//! Copy count values starting at chunk offset `offset` into
//! out[out_offset .. out_offset + count). Same boundary-crossing logic as
//! Append, in the opposite direction.
//!
//! Tests: Lab1StorageTest.ColumnChunk* (same tests exercise both directions)
//!
//! ----------------------------------------------------------------------------
//! Task L1.T3 - the zone map
//!
//! Maintain min/max over all non-NULL values ever appended (UpdateZoneMap
//! inside Append), then implement CheckZoneMap(constant, comparison) so it
//! returns FALSE exactly when the predicate `column OP constant` cannot be
//! true for ANY row in this chunk:
//!
//!   column = c   impossible when c < min or c > max
//!   column < c   impossible when min >= c      (etc. for all six operators)
//!
//! Get every boundary right: column >= max IS possible (the max row itself),
//! and an EMPTY chunk (HasZoneMap() == false) must always return true - "no
//! information" means "cannot prune", never "prune everything".
//!
//! Hint: write the six ExpressionType cases as a small switch over
//!       COMPARE_EQUAL .. COMPARE_GREATER_THAN_OR_EQUAL, using
//!       Value::Equals / Value::LessThan.
//!
//! Tests: Lab1StorageTest.ZoneMap* (7 tests, including boundary inclusivity)
//! ============================================================================
class ColumnChunk {
public:
	explicit ColumnChunk(const LogicalType &type);

	const LogicalType &GetType() const;
	idx_t Count() const;

	//! Append count values from data[source_offset .. source_offset + count)
	void Append(Vector &data, idx_t source_offset, idx_t count);
	//! Read count values from [offset, offset + count) into out[out_offset ..]
	void Scan(idx_t offset, idx_t count, Vector &out, idx_t out_offset) const;

	//! Zone map support: min/max over all non-NULL values appended so far.
	bool HasZoneMap() const;
	const Value &Min() const;
	const Value &Max() const;
	//! Returns false if the zone map proves no row can satisfy `column OP constant`
	bool CheckZoneMap(const Value &constant, ExpressionType comparison) const;

private:
	void UpdateZoneMap(const Value &value);

	LogicalType type_;
	std::vector<std::unique_ptr<Vector>> blocks_;
	idx_t count_ = 0;

	bool has_zone_map_ = false;
	Value min_;
	Value max_;
};

} // namespace tiny_duckdb
