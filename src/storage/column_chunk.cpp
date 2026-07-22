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
	// [SOLUTION BEGIN L1.T1]
	idx_t remaining = count;
	idx_t source_index = source_offset;
	while (remaining > 0) {
		if (blocks_.empty() || count_ % STANDARD_VECTOR_SIZE == 0) {
			blocks_.push_back(std::make_unique<Vector>(type_));
		}
		Vector &target = *blocks_.back();
		const idx_t target_offset = count_ % STANDARD_VECTOR_SIZE;
		const idx_t batch = std::min(remaining, STANDARD_VECTOR_SIZE - target_offset);
		for (idx_t i = 0; i < batch; i++) {
			const Value value = data.GetValue(source_index + i);
			target.SetValue(target_offset + i, value);
			UpdateZoneMap(value);
		}
		source_index += batch;
		remaining -= batch;
		count_ += batch;
	}
	// [SOLUTION END]
}

void ColumnChunk::Scan(idx_t offset, idx_t count, Vector &out, idx_t out_offset) const {
	if (offset + count > count_) {
		throw StorageException("ColumnChunk::Scan out of range");
	}
	// [SOLUTION BEGIN L1.T2]
	idx_t remaining = count;
	idx_t chunk_index = offset;
	idx_t out_index = out_offset;
	while (remaining > 0) {
		const idx_t block_index = chunk_index / STANDARD_VECTOR_SIZE;
		const idx_t block_offset = chunk_index % STANDARD_VECTOR_SIZE;
		const idx_t batch = std::min(remaining, STANDARD_VECTOR_SIZE - block_offset);
		const Vector &source = *blocks_[block_index];
		for (idx_t i = 0; i < batch; i++) {
			out.SetValue(out_index + i, source.GetValue(block_offset + i));
		}
		chunk_index += batch;
		out_index += batch;
		remaining -= batch;
	}
	// [SOLUTION END]
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
	// [SOLUTION BEGIN L1.T3]
	if (value.IsNull()) {
		return;
	}
	if (!has_zone_map_) {
		min_ = value;
		max_ = value;
		has_zone_map_ = true;
		return;
	}
	if (Value::LessThan(value, min_)) {
		min_ = value;
	}
	if (Value::LessThan(max_, value)) {
		max_ = value;
	}
	// [SOLUTION END]
}

bool ColumnChunk::CheckZoneMap(const Value &constant, ExpressionType comparison) const {
	// [SOLUTION BEGIN L1.T3]
	if (!has_zone_map_ || constant.IsNull()) {
		// no information (or a NULL constant): the chunk might match
		return true;
	}
	switch (comparison) {
	case ExpressionType::COMPARE_EQUAL:
		// can only match if constant is within [min, max]
		return !Value::LessThan(constant, min_) && !Value::LessThan(max_, constant);
	case ExpressionType::COMPARE_NOT_EQUAL:
		// can only fail to match if every value equals the constant
		return !(Value::Equals(min_, max_) && Value::Equals(min_, constant));
	case ExpressionType::COMPARE_GREATER_THAN:
		// column > constant possible iff max > constant
		return Value::LessThan(constant, max_);
	case ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL:
		return !Value::LessThan(max_, constant);
	case ExpressionType::COMPARE_LESS_THAN:
		// column < constant possible iff min < constant
		return Value::LessThan(min_, constant);
	case ExpressionType::COMPARE_LESS_THAN_OR_EQUAL:
		return !Value::LessThan(constant, min_);
	default:
		return true;
	}
	// [SOLUTION END]
}

} // namespace tiny_duckdb
