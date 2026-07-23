#include "tiny_duckdb/common/types.hpp"

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

LogicalType::LogicalType() : id_(LogicalTypeId::INTEGER), vector_size_(0) {
}

LogicalType::LogicalType(LogicalTypeId id, idx_t vector_size) : id_(id), vector_size_(vector_size) {
	if (id_ == LogicalTypeId::VECTOR && vector_size_ == 0) {
		throw StorageException("VECTOR dimension must be greater than zero");
	}
}

LogicalTypeId LogicalType::Id() const {
	return id_;
}

idx_t LogicalType::VectorSize() const {
	if (id_ != LogicalTypeId::VECTOR) {
		throw StorageException("VectorSize called on non-VECTOR type");
	}
	return vector_size_;
}

idx_t LogicalType::FixedSize() const {
	switch (id_) {
	case LogicalTypeId::BOOLEAN:
		return sizeof(bool);
	case LogicalTypeId::INTEGER:
		return sizeof(int32_t);
	case LogicalTypeId::BIGINT:
		return sizeof(int64_t);
	case LogicalTypeId::DOUBLE:
		return sizeof(double);
	case LogicalTypeId::VARCHAR:
	case LogicalTypeId::VECTOR:
		return 0;
	}
	throw StorageException("Unknown logical type");
}

bool LogicalType::IsNumeric() const {
	return id_ == LogicalTypeId::INTEGER || id_ == LogicalTypeId::BIGINT || id_ == LogicalTypeId::DOUBLE;
}

bool LogicalType::IsIntegral() const {
	return id_ == LogicalTypeId::INTEGER || id_ == LogicalTypeId::BIGINT;
}

std::string LogicalType::ToString() const {
	switch (id_) {
	case LogicalTypeId::BOOLEAN:
		return "BOOLEAN";
	case LogicalTypeId::INTEGER:
		return "INTEGER";
	case LogicalTypeId::BIGINT:
		return "BIGINT";
	case LogicalTypeId::DOUBLE:
		return "DOUBLE";
	case LogicalTypeId::VARCHAR:
		return "VARCHAR";
	case LogicalTypeId::VECTOR:
		return "VECTOR(" + std::to_string(vector_size_) + ")";
	}
	return "UNKNOWN";
}

LogicalType LogicalType::Boolean() {
	return LogicalType(LogicalTypeId::BOOLEAN);
}

LogicalType LogicalType::Integer() {
	return LogicalType(LogicalTypeId::INTEGER);
}

LogicalType LogicalType::BigInt() {
	return LogicalType(LogicalTypeId::BIGINT);
}

LogicalType LogicalType::Double() {
	return LogicalType(LogicalTypeId::DOUBLE);
}

LogicalType LogicalType::Varchar() {
	return LogicalType(LogicalTypeId::VARCHAR);
}

LogicalType LogicalType::Vector(idx_t size) {
	return LogicalType(LogicalTypeId::VECTOR, size);
}

bool LogicalType::operator==(const LogicalType &rhs) const {
	return id_ == rhs.id_ && (id_ != LogicalTypeId::VECTOR || vector_size_ == rhs.vector_size_);
}

bool LogicalType::operator!=(const LogicalType &rhs) const {
	return !(*this == rhs);
}

} // namespace tiny_duckdb
