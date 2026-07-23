#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/common/value.hpp"

namespace tiny_duckdb {

//! Bitmask tracking NULL values inside a vector. Bit set = valid, bit clear = NULL.
class ValidityMask {
public:
	ValidityMask();

	//! Mark every entry as valid
	void Reset();
	void SetValid(idx_t index);
	void SetInvalid(idx_t index);
	bool IsValid(idx_t index) const;
	bool AllValid(idx_t count) const;
	//! Number of valid (non-NULL) entries in [0, count)
	idx_t CountValid(idx_t count) const;

private:
	static constexpr idx_t BITS_PER_WORD = 64;
	static constexpr idx_t WORD_COUNT = STANDARD_VECTOR_SIZE / BITS_PER_WORD;

	uint64_t words_[WORD_COUNT];
};

//! A selection vector: a list of row indexes, used to represent filtered data
//! without copying it (DuckDB's core vectorization trick).
class SelectionVector {
public:
	SelectionVector();
	explicit SelectionVector(idx_t count);

	void set_index(idx_t position, idx_t index);
	idx_t get_index(idx_t position) const;
	idx_t size() const;
	void resize(idx_t count);
	std::vector<idx_t> ToVector(idx_t count) const;

private:
	std::vector<sel_t> data_;
};

//! DuckDB-style flat vector: STANDARD_VECTOR_SIZE values of one type + a validity mask.
//! VARCHAR data lives in a per-vector string heap.
class Vector {
public:
	explicit Vector(const LogicalType &type);

	const LogicalType &GetType() const;
	ValidityMask &GetValidity();
	const ValidityMask &GetValidity() const;

	Value GetValue(idx_t index) const;
	void SetValue(idx_t index, const Value &value);

	//! Raw fixed-size data (nullptr for VARCHAR)
	uint8_t *GetData();
	template <class T>
	T *GetData() {
		return reinterpret_cast<T *>(GetData());
	}

	void Reset();

private:
	LogicalType type_;
	std::unique_ptr<uint8_t[]> data_;
	std::vector<std::string> string_heap_;
	std::vector<std::vector<double>> vector_heap_;
	ValidityMask validity_;
};

} // namespace tiny_duckdb
