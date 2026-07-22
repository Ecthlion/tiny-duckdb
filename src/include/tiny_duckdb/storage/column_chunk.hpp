#pragma once

#include <memory>
#include <vector>

#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/common/value.hpp"
#include "tiny_duckdb/common/vector.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 1 - Columnar storage
//!
//! A ColumnChunk stores ONE column of ONE row group, as a list of Vectors
//! (column-at-a-time layout, like DuckDB's column segments). It also
//! maintains a small zone map (min/max) that the scan can use to skip entire
//! row groups.
//!
//! Task L1.T1: Append() - copy count values from a source vector, starting at
//!             source_offset, into the chunk (append new block vectors as
//!             needed). Do not forget the validity mask and VARCHARs.
//! Task L1.T2: Scan() - copy count values starting at chunk offset `offset`
//!             into the output vector (starting at out_offset).
//! Task L1.T3: zone maps - maintain min/max inside Append, and implement
//!             CheckZoneMap so it returns false when the predicate
//!             `column OP constant` cannot be true for any row in this chunk.
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
