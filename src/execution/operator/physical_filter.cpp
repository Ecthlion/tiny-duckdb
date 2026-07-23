#include "tiny_duckdb/execution/operator/physical_filter.hpp"

#include "tiny_duckdb/execution/expression_executor.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 3 - TASK #2: the mid-pipeline OPERATORs (PhysicalFilter / Projection)
//!
//! An OPERATOR sits in the middle of a pipeline: it receives one DataChunk
//! from its child (via Execute) and transforms it IN PLACE. It has no global
//! state of its own beyond the per-thread OperatorState.
//!
//! ----------------------------------------------------------------------------
//! Task L3.T2a - PhysicalFilter::Execute
//!
//! Evaluate the predicate against the chunk and REMOVE the rows that do not
//! qualify. The chunk must be compacted in place (no new chunk emitted).
//!
//! Hint: use Task #1's ExpressionExecutor::Select to fill the FilterState's
//!       SelectionVector, then DataChunk::Slice(sel, count) to compact.
//! Hint: if every row matches, slicing is unnecessary - skip it.
//! Note: the FilterState is per-thread, so the SelectionVector can be reused
//!       across chunks without any locking. That is the whole point of the
//!       source/operator/sink state split.
//!
//! Tests: Lab3ExecutionTest.WhereFilter / WhereConjunction / Filter*
//!
//! ----------------------------------------------------------------------------
//! Task L3.T2b - PhysicalProjection::Execute
//!
//! Evaluate every select-list expression against the input chunk and replace
//! the chunk with the results. The output schema is `types` (set in the
//! constructor); expression #i produces output column #i.
//!
//! Hint: ProjectionState::output is a scratch chunk initialized with `types`.
//!       Evaluate into its vectors, set its cardinality, then move it into
//!       `chunk` - and re-Initialize the scratch chunk for the next input.
//! Note: cardinality comes from the INPUT chunk (expressions never change
//!       the number of rows).
//!
//! Tests: Lab3ExecutionTest.SelectStar / ProjectionArithmetic / Projection*
//! ============================================================================

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

std::unique_ptr<OperatorState> PhysicalFilter::GetOperatorState(ExecutionContext & /*context*/) {
	return std::make_unique<FilterState>();
}

OperatorResultType PhysicalFilter::Execute(ExecutionContext & /*context*/, DataChunk &chunk, OperatorState &state) {
	// TODO(L3.T2): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T2 not implemented yet");
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

std::unique_ptr<OperatorState> PhysicalProjection::GetOperatorState(ExecutionContext & /*context*/) {
	auto result = std::make_unique<ProjectionState>();
	result->output.Initialize(types);
	return result;
}

OperatorResultType PhysicalProjection::Execute(ExecutionContext & /*context*/, DataChunk &chunk,
                                               OperatorState &state) {
	// TODO(L3.T2): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T2 not implemented yet");
}

} // namespace tiny_duckdb
