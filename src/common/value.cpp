#include "tiny_duckdb/common/value.hpp"

#include <cmath>
#include <functional>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

Value::Value() : Value(LogicalType::Integer()) {
}

Value::Value(const LogicalType &type)
    : type_(type), is_null_(true), bool_value_(false), int_value_(0), bigint_value_(0), double_value_(0) {
}

Value Value::Null(const LogicalType &type) {
	return Value(type);
}

Value Value::Boolean(bool val) {
	Value result(LogicalType::Boolean());
	result.is_null_ = false;
	result.bool_value_ = val;
	return result;
}

Value Value::Integer(int32_t val) {
	Value result(LogicalType::Integer());
	result.is_null_ = false;
	result.int_value_ = val;
	return result;
}

Value Value::BigInt(int64_t val) {
	Value result(LogicalType::BigInt());
	result.is_null_ = false;
	result.bigint_value_ = val;
	return result;
}

Value Value::Double(double val) {
	Value result(LogicalType::Double());
	result.is_null_ = false;
	result.double_value_ = val;
	return result;
}

Value Value::Varchar(const std::string &val) {
	Value result(LogicalType::Varchar());
	result.is_null_ = false;
	result.string_value_ = val;
	return result;
}

bool Value::IsNull() const {
	return is_null_;
}

const LogicalType &Value::GetType() const {
	return type_;
}

bool Value::GetBoolean() const {
	if (type_.Id() != LogicalTypeId::BOOLEAN) {
		throw ExecutorException("GetBoolean on non-boolean value");
	}
	return bool_value_;
}

int32_t Value::GetInteger() const {
	if (type_.Id() != LogicalTypeId::INTEGER) {
		throw ExecutorException("GetInteger on non-integer value");
	}
	return int_value_;
}

int64_t Value::GetBigInt() const {
	if (type_.Id() != LogicalTypeId::BIGINT) {
		throw ExecutorException("GetBigInt on non-bigint value");
	}
	return bigint_value_;
}

double Value::GetDouble() const {
	if (type_.Id() != LogicalTypeId::DOUBLE) {
		throw ExecutorException("GetDouble on non-double value");
	}
	return double_value_;
}

const std::string &Value::GetVarchar() const {
	if (type_.Id() != LogicalTypeId::VARCHAR) {
		throw ExecutorException("GetVarchar on non-varchar value");
	}
	return string_value_;
}

double Value::GetNumeric() const {
	switch (type_.Id()) {
	case LogicalTypeId::INTEGER:
		return static_cast<double>(int_value_);
	case LogicalTypeId::BIGINT:
		return static_cast<double>(bigint_value_);
	case LogicalTypeId::DOUBLE:
		return double_value_;
	default:
		throw ExecutorException("GetNumeric on non-numeric value of type " + type_.ToString());
	}
}

int64_t Value::GetIntegral() const {
	switch (type_.Id()) {
	case LogicalTypeId::INTEGER:
		return static_cast<int64_t>(int_value_);
	case LogicalTypeId::BIGINT:
		return bigint_value_;
	default:
		throw ExecutorException("GetIntegral on non-integral value of type " + type_.ToString());
	}
}

std::string Value::ToString() const {
	if (is_null_) {
		return "NULL";
	}
	switch (type_.Id()) {
	case LogicalTypeId::BOOLEAN:
		return bool_value_ ? "true" : "false";
	case LogicalTypeId::INTEGER:
		return std::to_string(int_value_);
	case LogicalTypeId::BIGINT:
		return std::to_string(bigint_value_);
	case LogicalTypeId::DOUBLE: {
		std::string str = std::to_string(double_value_);
		str.erase(str.find_last_not_of('0') + 1);
		if (!str.empty() && str.back() == '.') {
			str.push_back('0');
		}
		return str;
	}
	case LogicalTypeId::VARCHAR:
		return string_value_;
	}
	return "?";
}

bool Value::Equals(const Value &left, const Value &right) {
	if (left.is_null_ || right.is_null_) {
		return left.is_null_ && right.is_null_;
	}
	if (left.type_.IsNumeric() && right.type_.IsNumeric()) {
		return left.GetNumeric() == right.GetNumeric();
	}
	if (left.type_ != right.type_) {
		return false;
	}
	if (left.type_.Id() == LogicalTypeId::BOOLEAN) {
		return left.bool_value_ == right.bool_value_;
	}
	return left.string_value_ == right.string_value_;
}

bool Value::LessThan(const Value &left, const Value &right) {
	if (left.is_null_ || right.is_null_) {
		return left.is_null_ && !right.is_null_;
	}
	if (left.type_.IsNumeric() && right.type_.IsNumeric()) {
		return left.GetNumeric() < right.GetNumeric();
	}
	if (left.type_ != right.type_) {
		throw ExecutorException("Cannot compare " + left.type_.ToString() + " with " + right.type_.ToString());
	}
	if (left.type_.Id() == LogicalTypeId::BOOLEAN) {
		return !left.bool_value_ && right.bool_value_;
	}
	return left.string_value_ < right.string_value_;
}

uint64_t Value::Hash() const {
	if (is_null_) {
		return 0x9e3779b97f4a7c15ULL;
	}
	if (type_.IsNumeric()) {
		return std::hash<double> {}(GetNumeric());
	}
	if (type_.Id() == LogicalTypeId::BOOLEAN) {
		return std::hash<bool> {}(bool_value_);
	}
	return std::hash<std::string> {}(string_value_);
}

LogicalType Value::MaxNumericType(const LogicalType &left, const LogicalType &right) {
	if (left.Id() == LogicalTypeId::DOUBLE || right.Id() == LogicalTypeId::DOUBLE) {
		return LogicalType::Double();
	}
	if (left.Id() == LogicalTypeId::BIGINT || right.Id() == LogicalTypeId::BIGINT) {
		return LogicalType::BigInt();
	}
	return LogicalType::Integer();
}

template <class INT_OP, class DOUBLE_OP>
static Value NumericBinaryOp(const Value &left, const Value &right, INT_OP int_op, DOUBLE_OP double_op,
                             bool force_double) {
	if (left.IsNull() || right.IsNull()) {
		return Value::Null(Value::MaxNumericType(left.GetType(), right.GetType()));
	}
	if (!left.GetType().IsNumeric() || !right.GetType().IsNumeric()) {
		throw ExecutorException("Arithmetic on non-numeric value");
	}
	const LogicalType result_type = Value::MaxNumericType(left.GetType(), right.GetType());
	if (force_double || result_type.Id() == LogicalTypeId::DOUBLE) {
		return Value::Double(double_op(left.GetNumeric(), right.GetNumeric()));
	}
	const int64_t result = int_op(left.GetIntegral(), right.GetIntegral());
	if (result_type.Id() == LogicalTypeId::BIGINT) {
		return Value::BigInt(result);
	}
	return Value::Integer(static_cast<int32_t>(result));
}

Value Value::Add(const Value &left, const Value &right) {
	return NumericBinaryOp(
	    left, right, [](int64_t a, int64_t b) { return a + b; }, [](double a, double b) { return a + b; }, false);
}

Value Value::Subtract(const Value &left, const Value &right) {
	return NumericBinaryOp(
	    left, right, [](int64_t a, int64_t b) { return a - b; }, [](double a, double b) { return a - b; }, false);
}

Value Value::Multiply(const Value &left, const Value &right) {
	return NumericBinaryOp(
	    left, right, [](int64_t a, int64_t b) { return a * b; }, [](double a, double b) { return a * b; }, false);
}

Value Value::Divide(const Value &left, const Value &right) {
	return NumericBinaryOp(
	    left, right, [](int64_t a, int64_t b) { return b == 0 ? 0 : a / b; },
	    [](double a, double b) { return b == 0 ? std::nan("") : a / b; }, true);
}

std::ostream &operator<<(std::ostream &os, const Value &value) {
	os << value.ToString();
	return os;
}

bool operator==(const Value &left, const Value &right) {
	return Value::Equals(left, right);
}

bool operator!=(const Value &left, const Value &right) {
	return !Value::Equals(left, right);
}

} // namespace tiny_duckdb
