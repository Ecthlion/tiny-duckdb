#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "tiny_duckdb/binder/bound_expression.hpp"
#include "tiny_duckdb/common/types.hpp"

namespace tiny_duckdb {

class TableData;

enum class LogicalOperatorType : uint8_t {
	LOGICAL_GET = 0,
	LOGICAL_FILTER = 1,
	LOGICAL_PROJECTION = 2,
	LOGICAL_AGGREGATE = 3,
	LOGICAL_JOIN = 4,
	LOGICAL_ORDER = 5,
	LOGICAL_LIMIT = 6
};

//! ---------------------------------------------------------------------------
//! Logical plan nodes (produced by the Binder, Lab 2; turned into physical
//! operators by the PhysicalPlanGenerator, Lab 3).
//! ---------------------------------------------------------------------------
class LogicalOperator {
public:
	explicit LogicalOperator(LogicalOperatorType type) : type(type) {
	}
	virtual ~LogicalOperator() = default;

	LogicalOperatorType type;
	std::vector<std::unique_ptr<LogicalOperator>> children;
	//! Output schema
	std::vector<std::string> names;
	std::vector<LogicalType> types;

	template <class T>
	T &Cast() {
		return static_cast<T &>(*this);
	}
	template <class T>
	const T &Cast() const {
		return static_cast<const T &>(*this);
	}
};

//! Scan of a base table; column_ids refers to table column indexes
class LogicalGet : public LogicalOperator {
public:
	LogicalGet(TableData &table, std::vector<idx_t> column_ids);

	TableData &table;
	std::vector<idx_t> column_ids;
};

class LogicalFilter : public LogicalOperator {
public:
	explicit LogicalFilter(std::unique_ptr<BoundExpression> predicate_p)
	    : LogicalOperator(LogicalOperatorType::LOGICAL_FILTER), predicate(std::move(predicate_p)) {
	}

	std::unique_ptr<BoundExpression> predicate;
};

class LogicalProjection : public LogicalOperator {
public:
	explicit LogicalProjection(std::vector<std::unique_ptr<BoundExpression>> expressions_p)
	    : LogicalOperator(LogicalOperatorType::LOGICAL_PROJECTION), expressions(std::move(expressions_p)) {
	}

	std::vector<std::unique_ptr<BoundExpression>> expressions;
};

//! GROUP BY + aggregates. Output: group columns first, then aggregate results.
class LogicalAggregate : public LogicalOperator {
public:
	LogicalAggregate(std::vector<std::unique_ptr<BoundExpression>> groups_p,
	                 std::vector<std::unique_ptr<BoundAggregateExpression>> aggregates_p)
	    : LogicalOperator(LogicalOperatorType::LOGICAL_AGGREGATE), groups(std::move(groups_p)),
	      aggregates(std::move(aggregates_p)) {
	}

	std::vector<std::unique_ptr<BoundExpression>> groups;
	std::vector<std::unique_ptr<BoundAggregateExpression>> aggregates;
};

//! INNER equi-join. Left child = probe side, right child = build side.
class LogicalJoin : public LogicalOperator {
public:
	LogicalJoin() : LogicalOperator(LogicalOperatorType::LOGICAL_JOIN) {
	}

	//! Equi-conditions: left key over left child output, right key over right child output
	std::vector<std::pair<std::unique_ptr<BoundExpression>, std::unique_ptr<BoundExpression>>> conditions;
};

class LogicalOrder : public LogicalOperator {
public:
	LogicalOrder() : LogicalOperator(LogicalOperatorType::LOGICAL_ORDER) {
	}

	//! (output column index, ascending)
	std::vector<std::pair<idx_t, bool>> keys;
};

class LogicalLimit : public LogicalOperator {
public:
	explicit LogicalLimit(int64_t limit_p) : LogicalOperator(LogicalOperatorType::LOGICAL_LIMIT), limit(limit_p) {
	}

	int64_t limit;
};

} // namespace tiny_duckdb
