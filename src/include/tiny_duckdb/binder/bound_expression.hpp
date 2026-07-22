#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/common/value.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 2 - bound expressions: fully typed, names resolved to column indexes
//! ============================================================================
class BoundExpression {
public:
	BoundExpression(ExpressionType type, LogicalType return_type)
	    : type(type), return_type(std::move(return_type)) {
	}
	virtual ~BoundExpression() = default;

	ExpressionType type;
	LogicalType return_type;

	virtual std::string ToString() const = 0;

	//! DuckDB-style downcast helper
	template <class T>
	T &Cast() {
		return static_cast<T &>(*this);
	}
	template <class T>
	const T &Cast() const {
		return static_cast<const T &>(*this);
	}
};

//! A reference to a column of the child operator, by index
class BoundColumnRefExpression : public BoundExpression {
public:
	BoundColumnRefExpression(std::string name, idx_t column_index_p, LogicalType return_type)
	    : BoundExpression(ExpressionType::COLUMN_REF, std::move(return_type)), name(std::move(name)),
	      column_index(column_index_p) {
	}

	std::string ToString() const override {
		return name;
	}

	std::string name;
	idx_t column_index;
};

class BoundConstantExpression : public BoundExpression {
public:
	explicit BoundConstantExpression(const Value &value_p)
	    : BoundExpression(ExpressionType::VALUE_CONSTANT, value_p.GetType()), value(value_p) {
	}

	std::string ToString() const override {
		return value.ToString();
	}

	Value value;
};

class BoundComparisonExpression : public BoundExpression {
public:
	BoundComparisonExpression(ExpressionType comparison, std::unique_ptr<BoundExpression> left_p,
	                          std::unique_ptr<BoundExpression> right_p)
	    : BoundExpression(comparison, LogicalType::Boolean()), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::string ToString() const override {
		return "(cmp)";
	}

	std::unique_ptr<BoundExpression> left;
	std::unique_ptr<BoundExpression> right;
};

class BoundConjunctionExpression : public BoundExpression {
public:
	BoundConjunctionExpression(ExpressionType conjunction, std::unique_ptr<BoundExpression> left_p,
	                           std::unique_ptr<BoundExpression> right_p)
	    : BoundExpression(conjunction, LogicalType::Boolean()), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::string ToString() const override {
		return "(conj)";
	}

	std::unique_ptr<BoundExpression> left;
	std::unique_ptr<BoundExpression> right;
};

class BoundOperatorExpression : public BoundExpression {
public:
	BoundOperatorExpression(ExpressionType op, std::unique_ptr<BoundExpression> left_p,
	                        std::unique_ptr<BoundExpression> right_p, LogicalType return_type)
	    : BoundExpression(op, std::move(return_type)), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::string ToString() const override {
		return "(op)";
	}

	std::unique_ptr<BoundExpression> left;
	std::unique_ptr<BoundExpression> right;
};

//! An aggregate function call; child == nullptr means count(*)
class BoundAggregateExpression : public BoundExpression {
public:
	BoundAggregateExpression(ExpressionType aggregate, std::unique_ptr<BoundExpression> child_p,
	                         LogicalType return_type)
	    : BoundExpression(aggregate, std::move(return_type)), child(std::move(child_p)) {
	}

	std::string ToString() const override {
		return "(agg)";
	}

	std::unique_ptr<BoundExpression> child;
};

} // namespace tiny_duckdb
