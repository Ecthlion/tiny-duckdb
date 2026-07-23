#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "tiny_duckdb/common/types.hpp"

namespace tiny_duckdb {

//! A single SQL value. Owns its string payload for VARCHAR.
//! Numeric values of different types are automatically coerced for comparison/arithmetic.
class Value {
public:
	//! Create a NULL value of the given type
	static Value Null(const LogicalType &type);
	static Value Boolean(bool val);
	static Value Integer(int32_t val);
	static Value BigInt(int64_t val);
	static Value Double(double val);
	static Value Varchar(const std::string &val);
	static Value Vector(std::vector<double> val);

	//! Create a NULL INTEGER value
	Value();

	bool IsNull() const;
	const LogicalType &GetType() const;

	bool GetBoolean() const;
	int32_t GetInteger() const;
	int64_t GetBigInt() const;
	double GetDouble() const;
	const std::string &GetVarchar() const;
	const std::vector<double> &GetVector() const;
	//! Numeric coercion: INTEGER/BIGINT/DOUBLE -> double. Throws for other types.
	double GetNumeric() const;
	int64_t GetIntegral() const;

	std::string ToString() const;

	//! Grouping semantics equality: NULL == NULL is true, numeric types coerce.
	static bool Equals(const Value &left, const Value &right);
	//! Grouping semantics less-than (NULLs first), numeric types coerce.
	static bool LessThan(const Value &left, const Value &right);
	//! Hash consistent with Equals (numeric types share a hash space).
	uint64_t Hash() const;

	//! Arithmetic with numeric coercion. Division always produces a DOUBLE.
	//! NULL in -> NULL out.
	static Value Add(const Value &left, const Value &right);
	static Value Subtract(const Value &left, const Value &right);
	static Value Multiply(const Value &left, const Value &right);
	static Value Divide(const Value &left, const Value &right);

	//! The wider of two numeric types (used to pick the result type of arithmetic)
	static LogicalType MaxNumericType(const LogicalType &left, const LogicalType &right);

private:
	explicit Value(const LogicalType &type);

	LogicalType type_;
	bool is_null_;
	bool bool_value_;
	int32_t int_value_;
	int64_t bigint_value_;
	double double_value_;
	std::string string_value_;
	std::vector<double> vector_value_;
};

std::ostream &operator<<(std::ostream &os, const Value &value);

//! Convenience operators delegating to Value::Equals (grouping semantics)
bool operator==(const Value &left, const Value &right);
bool operator!=(const Value &left, const Value &right);

} // namespace tiny_duckdb
