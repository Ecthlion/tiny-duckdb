#pragma once

#include <cstdint>
#include <ostream>
#include <string>

namespace tiny_duckdb {

//! Index/offset type used throughout the system (DuckDB convention)
using idx_t = uint64_t;
//! Selection index type used by selection vectors
using sel_t = uint32_t;

//! Number of tuples in one vector (DuckDB uses 2048 as well)
static constexpr idx_t STANDARD_VECTOR_SIZE = 2048;
//! Number of tuples per row group (much smaller than DuckDB's 122880 so tests stay fast)
static constexpr idx_t ROW_GROUP_SIZE = 4 * STANDARD_VECTOR_SIZE;

enum class LogicalTypeId : uint8_t {
	BOOLEAN = 0,
	INTEGER = 1,
	BIGINT = 2,
	DOUBLE = 3,
	VARCHAR = 4,
	//! A fixed-length embedding made of DOUBLE elements (Lab 5).
	VECTOR = 5
};

//! Simplified logical type. VARCHAR and VECTOR are variable-size in our in-memory
//! representation; VECTOR still has a fixed logical dimension.
class LogicalType {
public:
	LogicalType();
	explicit LogicalType(LogicalTypeId id, idx_t vector_size = 0);

	LogicalTypeId Id() const;
	//! Number of elements in a VECTOR. Throws for non-vector types.
	idx_t VectorSize() const;
	//! Size in bytes for fixed-size types, 0 for VARCHAR
	idx_t FixedSize() const;
	bool IsNumeric() const;
	bool IsIntegral() const;
	std::string ToString() const;

	static LogicalType Boolean();
	static LogicalType Integer();
	static LogicalType BigInt();
	static LogicalType Double();
	static LogicalType Varchar();
	static LogicalType Vector(idx_t size);

	bool operator==(const LogicalType &rhs) const;
	bool operator!=(const LogicalType &rhs) const;

private:
	LogicalTypeId id_;
	idx_t vector_size_;
};

inline std::ostream &operator<<(std::ostream &os, const LogicalType &type) {
	os << type.ToString();
	return os;
}

} // namespace tiny_duckdb
