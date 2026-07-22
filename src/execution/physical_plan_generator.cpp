#include "tiny_duckdb/execution/physical_plan_generator.hpp"

#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/execution/operator/physical_filter.hpp"
#include "tiny_duckdb/execution/operator/physical_hash_aggregate.hpp"
#include "tiny_duckdb/execution/operator/physical_hash_join.hpp"
#include "tiny_duckdb/execution/operator/physical_order_by.hpp"
#include "tiny_duckdb/execution/operator/physical_result_collector.hpp"
#include "tiny_duckdb/execution/operator/physical_table_scan.hpp"
#include "tiny_duckdb/storage/table_data.hpp"

namespace tiny_duckdb {

namespace {

//! Flip a comparison when the constant is on the left: `5 < x` -> `x > 5`
ExpressionType FlipComparison(ExpressionType comparison) {
	switch (comparison) {
	case ExpressionType::COMPARE_LESS_THAN:
		return ExpressionType::COMPARE_GREATER_THAN;
	case ExpressionType::COMPARE_LESS_THAN_OR_EQUAL:
		return ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL;
	case ExpressionType::COMPARE_GREATER_THAN:
		return ExpressionType::COMPARE_LESS_THAN;
	case ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL:
		return ExpressionType::COMPARE_LESS_THAN_OR_EQUAL;
	default:
		return comparison;
	}
}

//! Extract `column CMP constant` conjuncts as zone-map table filters.
//! column indexes refer to scan output positions; map them back to table
//! column ids through the LogicalGet's column_ids.
void ExtractTableFilters(BoundExpression &predicate, LogicalGet &get, std::vector<TableFilter> &out) {
	if (predicate.type == ExpressionType::CONJUNCTION_AND) {
		auto &conj = predicate.Cast<BoundConjunctionExpression>();
		ExtractTableFilters(*conj.left, get, out);
		ExtractTableFilters(*conj.right, get, out);
		return;
	}
	if (predicate.type < ExpressionType::COMPARE_EQUAL || predicate.type > ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL) {
		return;
	}
	auto &cmp = predicate.Cast<BoundComparisonExpression>();
	BoundColumnRefExpression *column = nullptr;
	BoundConstantExpression *constant = nullptr;
	ExpressionType comparison = predicate.type;
	if (cmp.left->type == ExpressionType::COLUMN_REF && cmp.right->type == ExpressionType::VALUE_CONSTANT) {
		column = &cmp.left->Cast<BoundColumnRefExpression>();
		constant = &cmp.right->Cast<BoundConstantExpression>();
	} else if (cmp.left->type == ExpressionType::VALUE_CONSTANT && cmp.right->type == ExpressionType::COLUMN_REF) {
		column = &cmp.right->Cast<BoundColumnRefExpression>();
		constant = &cmp.left->Cast<BoundConstantExpression>();
		comparison = FlipComparison(comparison);
	}
	if (!column || !constant || constant->value.IsNull()) {
		return;
	}
	if (column->column_index >= get.column_ids.size()) {
		return;
	}
	TableFilter filter;
	filter.column_id = get.column_ids[column->column_index];
	filter.comparison = comparison;
	filter.constant = constant->value;
	out.push_back(std::move(filter));
}

} // namespace

std::unique_ptr<PhysicalOperator> PhysicalPlanGenerator::CreatePlan(LogicalOperator &op) {
	return CreatePlanInternal(op);
}

std::unique_ptr<PhysicalOperator> PhysicalPlanGenerator::CreatePlanInternal(LogicalOperator &op) {
	switch (op.type) {
	case LogicalOperatorType::LOGICAL_GET: {
		auto &get = op.Cast<LogicalGet>();
		return std::make_unique<PhysicalTableScan>(get.table, get.column_ids, get.types, get.names,
		                                      std::vector<TableFilter>());
	}
	case LogicalOperatorType::LOGICAL_FILTER: {
		auto &filter = op.Cast<LogicalFilter>();
		auto child = CreatePlanInternal(*op.children[0]);
		std::vector<TableFilter> table_filters;
		if (child->type == PhysicalOperatorType::TABLE_SCAN) {
			// L3.T3: push `column CMP constant` predicates down to the scan
			auto &get = op.children[0]->Cast<LogicalGet>();
			ExtractTableFilters(*filter.predicate, get, table_filters);
			child->Cast<PhysicalTableScan>().table_filters = std::move(table_filters);
		}
		auto result = std::make_unique<PhysicalFilter>(std::move(filter.predicate), op.types, op.names);
		result->children.push_back(std::move(child));
		return result;
	}
	case LogicalOperatorType::LOGICAL_PROJECTION: {
		auto &proj = op.Cast<LogicalProjection>();
		auto result = std::make_unique<PhysicalProjection>(std::move(proj.expressions), op.types, op.names);
		result->children.push_back(CreatePlanInternal(*op.children[0]));
		return result;
	}
	case LogicalOperatorType::LOGICAL_AGGREGATE: {
		auto &agg = op.Cast<LogicalAggregate>();
		auto result = std::make_unique<PhysicalHashAggregate>(std::move(agg.groups), std::move(agg.aggregates),
		                                                 op.types, op.names);
		result->children.push_back(CreatePlanInternal(*op.children[0]));
		return result;
	}
	case LogicalOperatorType::LOGICAL_JOIN: {
		auto &join = op.Cast<LogicalJoin>();
		auto left = CreatePlanInternal(*op.children[0]);
		auto right = CreatePlanInternal(*op.children[1]);
		auto result = std::make_unique<PhysicalHashJoin>(std::move(join.conditions), left->types, right->types,
		                                            op.names);
		result->children.push_back(std::move(left));
		result->children.push_back(std::move(right));
		return result;
	}
	case LogicalOperatorType::LOGICAL_ORDER: {
		auto &order = op.Cast<LogicalOrder>();
		auto result = std::make_unique<PhysicalOrderBy>(std::move(order.keys), op.types, op.names);
		result->children.push_back(CreatePlanInternal(*op.children[0]));
		return result;
	}
	case LogicalOperatorType::LOGICAL_LIMIT: {
		auto &limit = op.Cast<LogicalLimit>();
		auto result = std::make_unique<PhysicalLimit>(limit.limit, op.types, op.names);
		result->children.push_back(CreatePlanInternal(*op.children[0]));
		return result;
	}
	default:
		throw ExecutorException("PhysicalPlanGenerator: unsupported logical operator");
	}
}

} // namespace tiny_duckdb
