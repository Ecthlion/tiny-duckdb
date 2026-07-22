#include "tiny_duckdb/common/vector.hpp"

#include <cstring>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

ValidityMask::ValidityMask() {
	Reset();
}

void ValidityMask::Reset() {
	for (idx_t word = 0; word < WORD_COUNT; word++) {
		words_[word] = ~0ULL;
	}
}

void ValidityMask::SetValid(idx_t index) {
	words_[index / BITS_PER_WORD] |= (1ULL << (index % BITS_PER_WORD));
}

void ValidityMask::SetInvalid(idx_t index) {
	words_[index / BITS_PER_WORD] &= ~(1ULL << (index % BITS_PER_WORD));
}

bool ValidityMask::IsValid(idx_t index) const {
	return (words_[index / BITS_PER_WORD] >> (index % BITS_PER_WORD)) & 1ULL;
}

bool ValidityMask::AllValid(idx_t count) const {
	for (idx_t i = 0; i < count; i++) {
		if (!IsValid(i)) {
			return false;
		}
	}
	return true;
}

idx_t ValidityMask::CountValid(idx_t count) const {
	idx_t result = 0;
	for (idx_t i = 0; i < count; i++) {
		if (IsValid(i)) {
			result++;
		}
	}
	return result;
}

SelectionVector::SelectionVector() {
	data_.resize(STANDARD_VECTOR_SIZE);
}

SelectionVector::SelectionVector(idx_t count) {
	data_.resize(count);
}

void SelectionVector::set_index(idx_t position, idx_t index) {
	data_[position] = static_cast<sel_t>(index);
}

idx_t SelectionVector::get_index(idx_t position) const {
	return data_[position];
}

idx_t SelectionVector::size() const {
	return data_.size();
}

void SelectionVector::resize(idx_t count) {
	data_.resize(count);
}

std::vector<idx_t> SelectionVector::ToVector(idx_t count) const {
	std::vector<idx_t> result;
	result.reserve(count);
	for (idx_t i = 0; i < count; i++) {
		result.push_back(data_[i]);
	}
	return result;
}

Vector::Vector(const LogicalType &type) : type_(type) {
	const idx_t fixed_size = type_.FixedSize();
	if (fixed_size > 0) {
		data_ = std::make_unique<uint8_t[]>(fixed_size * STANDARD_VECTOR_SIZE);
		std::memset(data_.get(), 0, fixed_size * STANDARD_VECTOR_SIZE);
	} else {
		string_heap_.resize(STANDARD_VECTOR_SIZE);
	}
}

const LogicalType &Vector::GetType() const {
	return type_;
}

ValidityMask &Vector::GetValidity() {
	return validity_;
}

const ValidityMask &Vector::GetValidity() const {
	return validity_;
}

uint8_t *Vector::GetData() {
	return data_.get();
}

void Vector::Reset() {
	validity_.Reset();
	for (auto &entry : string_heap_) {
		entry.clear();
	}
}

Value Vector::GetValue(idx_t index) const {
	if (index >= STANDARD_VECTOR_SIZE) {
		throw StorageException("Vector::GetValue index out of range");
	}
	if (!validity_.IsValid(index)) {
		return Value::Null(type_);
	}
	switch (type_.Id()) {
	case LogicalTypeId::BOOLEAN:
		return Value::Boolean(reinterpret_cast<const bool *>(data_.get())[index]);
	case LogicalTypeId::INTEGER:
		return Value::Integer(reinterpret_cast<const int32_t *>(data_.get())[index]);
	case LogicalTypeId::BIGINT:
		return Value::BigInt(reinterpret_cast<const int64_t *>(data_.get())[index]);
	case LogicalTypeId::DOUBLE:
		return Value::Double(reinterpret_cast<const double *>(data_.get())[index]);
	case LogicalTypeId::VARCHAR:
		return Value::Varchar(string_heap_[index]);
	}
	throw StorageException("Unknown logical type in Vector::GetValue");
}

void Vector::SetValue(idx_t index, const Value &value) {
	if (index >= STANDARD_VECTOR_SIZE) {
		throw StorageException("Vector::SetValue index out of range");
	}
	if (value.GetType() != type_) {
		throw StorageException("Vector::SetValue type mismatch: vector is " + type_.ToString() + ", value is " +
		                       value.GetType().ToString());
	}
	if (value.IsNull()) {
		validity_.SetInvalid(index);
		return;
	}
	validity_.SetValid(index);
	switch (type_.Id()) {
	case LogicalTypeId::BOOLEAN:
		GetData<bool>()[index] = value.GetBoolean();
		return;
	case LogicalTypeId::INTEGER:
		GetData<int32_t>()[index] = value.GetInteger();
		return;
	case LogicalTypeId::BIGINT:
		GetData<int64_t>()[index] = value.GetBigInt();
		return;
	case LogicalTypeId::DOUBLE:
		GetData<double>()[index] = value.GetDouble();
		return;
	case LogicalTypeId::VARCHAR:
		string_heap_[index] = value.GetVarchar();
		return;
	}
	throw StorageException("Unknown logical type in Vector::SetValue");
}

} // namespace tiny_duckdb
