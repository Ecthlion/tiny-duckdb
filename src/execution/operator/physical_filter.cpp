#include "tiny_duckdb/execution/operator/physical_filter.hpp"

#include "tiny_duckdb/execution/expression_executor.hpp"

namespace tiny_duckdb {

//! Per-thread filter state: the reusable selection vector
class FilterState : public OperatorState {
public:
	SelectionVector sel;
};

PhysicalFilter::PhysicalFilter(std::unique_ptr<BoundExpression> predicate_p, std::vector<LogicalType> types_p,
                               std::vector<std::string> names_p)
    : PhysicalOperator(PhysicalOperatorType::FILTER, std::move(types_p)), predicate(std::move(predicate_p)) {
	names = std::move(names_p);
}

std::unique_ptr<OperatorState> PhysicalFilter::GetOperatorState(ExecutionContext &/*context*/) {
	return std::make_unique<FilterState>();
}

void PhysicalFilter::Execute(ExecutionContext &/*context*/, DataChunk &chunk, OperatorState &state) {
	auto &filter_state = state.Cast<FilterState>();
	// L3.T2: evaluate the predicate, compact the chunk with a selection vector
	idx_t match_count = ExpressionExecutor::Select(*predicate, chunk, filter_state.sel);
	if (match_count == chunk.size()) {
		return;
	}
	chunk.Slice(filter_state.sel, match_count);
}

class ProjectionState : public OperatorState {
public:
	DataChunk output;
};

PhysicalProjection::PhysicalProjection(std::vector<std::unique_ptr<BoundExpression>> expressions_p,
                                       std::vector<LogicalType> types_p, std::vector<std::string> names_p)
    : PhysicalOperator(PhysicalOperatorType::PROJECTION, std::move(types_p)),
      expressions(std::move(expressions_p)) {
	names = std::move(names_p);
}

std::unique_ptr<OperatorState> PhysicalProjection::GetOperatorState(ExecutionContext &/*context*/) {
	auto result = std::make_unique<ProjectionState>();
	result->output.Initialize(types);
	return result;
}

void PhysicalProjection::Execute(ExecutionContext &/*context*/, DataChunk &chunk, OperatorState &state) {
	auto &proj_state = state.Cast<ProjectionState>();
	// L3.T2: evaluate every select expression, then swap in the result chunk
	for (idx_t col = 0; col < expressions.size(); col++) {
		ExpressionExecutor::Evaluate(*expressions[col], chunk, proj_state.output.GetVector(col));
	}
	proj_state.output.SetCardinality(chunk.size());
	chunk = std::move(proj_state.output);
	// re-initialize the scratch chunk for the next input
	proj_state.output.Initialize(types);
}

} // namespace tiny_duckdb
