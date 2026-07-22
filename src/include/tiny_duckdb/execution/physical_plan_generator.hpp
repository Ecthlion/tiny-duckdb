#pragma once

#include <memory>

#include "tiny_duckdb/execution/physical_operator.hpp"
#include "tiny_duckdb/planner/logical_plan.hpp"

namespace tiny_duckdb {

//! Turns a logical plan into a physical plan. This is where simple physical
//! decisions live: equi-joins become hash joins, and `column CMP constant`
//! predicates under a scan are pushed down as zone-map table filters.
class PhysicalPlanGenerator {
public:
	std::unique_ptr<PhysicalOperator> CreatePlan(LogicalOperator &op);

private:
	std::unique_ptr<PhysicalOperator> CreatePlanInternal(LogicalOperator &op);
};

} // namespace tiny_duckdb
