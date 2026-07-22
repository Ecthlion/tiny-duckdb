#pragma once

#include <cstdint>

namespace tiny_duckdb {

//! Expression types shared by the parser, binder and execution engine.
enum class ExpressionType : uint8_t {
	// comparisons
	COMPARE_EQUAL = 0,
	COMPARE_NOT_EQUAL = 1,
	COMPARE_LESS_THAN = 2,
	COMPARE_LESS_THAN_OR_EQUAL = 3,
	COMPARE_GREATER_THAN = 4,
	COMPARE_GREATER_THAN_OR_EQUAL = 5,
	// conjunctions
	CONJUNCTION_AND = 6,
	CONJUNCTION_OR = 7,
	// arithmetic
	OPERATOR_ADD = 8,
	OPERATOR_SUBTRACT = 9,
	OPERATOR_MULTIPLY = 10,
	OPERATOR_DIVIDE = 11,
	// leaves
	VALUE_CONSTANT = 12,
	COLUMN_REF = 13,
	// aggregates
	AGGREGATE_COUNT = 14,
	AGGREGATE_COUNT_STAR = 15,
	AGGREGATE_SUM = 16,
	AGGREGATE_AVG = 17,
	AGGREGATE_MIN = 18,
	AGGREGATE_MAX = 19,
	// cast
	OPERATOR_CAST = 20
};

//! Physical operator types (used to decide sink/source behaviour)
enum class PhysicalOperatorType : uint8_t {
	TABLE_SCAN = 0,
	FILTER = 1,
	PROJECTION = 2,
	HASH_GROUP_BY = 3,
	HASH_JOIN = 4,
	ORDER_BY = 5,
	LIMIT = 6,
	RESULT_COLLECTOR = 7
};

} // namespace tiny_duckdb
