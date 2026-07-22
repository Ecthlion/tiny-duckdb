#include "tiny_duckdb/common/data_chunk.hpp"

#include <iomanip>
#include <sstream>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

void DataChunk::Initialize(const std::vector<LogicalType> &types) {
	data_.clear();
	types_ = types;
	for (const auto &type : types_) {
		data_.push_back(std::make_unique<Vector>(type));
	}
	count_ = 0;
}

idx_t DataChunk::size() const {
	return count_;
}

void DataChunk::SetCardinality(idx_t count) {
	if (count > STANDARD_VECTOR_SIZE) {
		throw ExecutorException("DataChunk cardinality exceeds STANDARD_VECTOR_SIZE");
	}
	count_ = count;
}

idx_t DataChunk::ColumnCount() const {
	return data_.size();
}

Vector &DataChunk::GetVector(idx_t column) {
	return *data_[column];
}

const Vector &DataChunk::GetVector(idx_t column) const {
	return *data_[column];
}

const std::vector<LogicalType> &DataChunk::GetTypes() const {
	return types_;
}

Value DataChunk::GetValue(idx_t column, idx_t row) const {
	return data_[column]->GetValue(row);
}

void DataChunk::SetValue(idx_t column, idx_t row, const Value &value) {
	data_[column]->SetValue(row, value);
}

void DataChunk::Slice(const SelectionVector &sel, idx_t count) {
	DataChunk copy;
	copy.Initialize(types_);
	copy.SetCardinality(count);
	for (idx_t col = 0; col < ColumnCount(); col++) {
		for (idx_t i = 0; i < count; i++) {
			copy.SetValue(col, i, GetValue(col, sel.get_index(i)));
		}
	}
	*this = std::move(copy);
}

void DataChunk::AppendRow(const std::vector<Value> &row) {
	if (row.size() != ColumnCount()) {
		throw ExecutorException("AppendRow: column count mismatch");
	}
	if (count_ >= STANDARD_VECTOR_SIZE) {
		throw ExecutorException("AppendRow: chunk is full");
	}
	for (idx_t col = 0; col < row.size(); col++) {
		SetValue(col, count_, row[col]);
	}
	count_++;
}

void DataChunk::CopyFrom(const DataChunk &other) {
	if (types_ != other.types_) {
		Initialize(other.types_);
	}
	Reset();
	SetCardinality(other.size());
	for (idx_t col = 0; col < other.ColumnCount(); col++) {
		for (idx_t row = 0; row < other.size(); row++) {
			SetValue(col, row, other.GetValue(col, row));
		}
	}
}

void DataChunk::Reset() {
	count_ = 0;
	for (auto &vector : data_) {
		vector->Reset();
	}
}

std::string DataChunk::ToString() const {
	std::ostringstream out;
	for (idx_t row = 0; row < count_; row++) {
		out << "|";
		for (idx_t col = 0; col < ColumnCount(); col++) {
			out << std::setw(12) << GetValue(col, row).ToString() << "|";
		}
		out << "\n";
	}
	return out.str();
}

} // namespace tiny_duckdb
