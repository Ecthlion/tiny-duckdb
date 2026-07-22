#pragma once

#include <memory>
#include <string>

#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/common/value.hpp"

namespace tiny_duckdb {

//! ---------------------------------------------------------------------------
//! Bound expressions: names resolved to column indexes, types inferred.
//! Evaluated by the ExpressionExecutor (Lab 3).
//! ---------------------------------------------------------------------------
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

//! A reference to column `column_index` of the input chunk
class BoundColumnRefExpression : public BoundExpression {
public:
	BoundColumnRefExpression(std::string name, idx_t column_index, LogicalType return_type)
	    : BoundExpression(ExpressionType::COLUMN_REF, std::move(return_type)), column_index(column_index),
	      name(std::move(name)) {
	}

	idx_t column_index;
	std::string name;

	std::string ToString() const override {
		return name;
	}
};

class BoundConstantExpression : public BoundExpression {
public:
	explicit BoundConstantExpression(Value value)
	    : BoundExpression(ExpressionType::VALUE_CONSTANT, value.GetType()), value(std::move(value)) {
	}

	Value value;

	std::string ToString() const override {
		return value.ToString();
	}
};

class BoundComparisonExpression : public BoundExpression {
public:
	BoundComparisonExpression(ExpressionType comparison, std::unique_ptr<BoundExpression> left_p,
	                          std::unique_ptr<BoundExpression> right_p)
	    : BoundExpression(comparison, LogicalType::Boolean()), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::unique_ptr<BoundExpression> left;
	std::unique_ptr<BoundExpression> right;

	std::string ToString() const override {
		return "(" + left->ToString() + " cmp " + right->ToString() + ")";
	}
};

class BoundConjunctionExpression : public BoundExpression {
public:
	BoundConjunctionExpression(ExpressionType conjunction, std::unique_ptr<BoundExpression> left_p,
	                           std::unique_ptr<BoundExpression> right_p)
	    : BoundExpression(conjunction, LogicalType::Boolean()), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::unique_ptr<BoundExpression> left;
	std::unique_ptr<BoundExpression> right;

	std::string ToString() const override {
		return "(" + left->ToString() + (type == ExpressionType::CONJUNCTION_AND ? " AND " : " OR ") +
		       right->ToString() + ")";
	}
};

class BoundOperatorExpression : public BoundExpression {
public:
	BoundOperatorExpression(ExpressionType op, std::unique_ptr<BoundExpression> left_p,
	                        std::unique_ptr<BoundExpression> right_p, LogicalType return_type)
	    : BoundExpression(op, std::move(return_type)), left(std::move(left_p)), right(std::move(right_p)) {
	}

	std::unique_ptr<BoundExpression> left;
	std::unique_ptr<BoundExpression> right;

	std::string ToString() const override {
		return "(" + left->ToString() + " op " + right->ToString() + ")";
	}
};

//! An aggregate function over child rows. child is null for count(*).
class BoundAggregateExpression : public BoundExpression {
public:
	BoundAggregateExpression(ExpressionType aggregate, std::unique_ptr<BoundExpression> child_p,
	                         LogicalType return_type)
	    : BoundExpression(aggregate, std::move(return_type)), child(std::move(child_p)) {
	}

	std::unique_ptr<BoundExpression> child;

	std::string ToString() const override {
		return "agg(" + (child ? child->ToString() : std::string("*")) + ")";
	}
};

} // namespace tiny_duckdb
