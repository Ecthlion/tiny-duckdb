#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "tiny_duckdb/binder/bound_expression.hpp"
#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/execution/physical_operator.hpp"

namespace tiny_duckdb {

//! Hash/equality functors for vector<Value> keys (grouping semantics)
struct VectorValueHash {
	size_t operator()(const std::vector<Value> &key) const;
};
struct VectorValueEqual {
	bool operator()(const std::vector<Value> &left, const std::vector<Value> &right) const;
};

//! ============================================================================
//! LAB 3 (Task L3.T5) - the hash join
//!
//! One operator, two roles, two pipelines:
//!
//!  * BUILD side (right child): the join acts as a SINK. Its pipeline drains
//!    the right child into a hash table keyed on the right join keys.
//!  * PROBE side (left child): the join acts as a mid-pipeline OPERATOR.
//!    Every incoming chunk is matched against the hash table.
//!
//! Because a probe chunk can produce more than STANDARD_VECTOR_SIZE output
//! rows, Execute() must be resumable: it stashes the pending input and its
//! match list in the operator state and continues where it left off.
//! ============================================================================
class PhysicalHashJoin : public PhysicalOperator {
public:
	PhysicalHashJoin(
	    std::vector<std::pair<std::unique_ptr<BoundExpression>, std::unique_ptr<BoundExpression>>> conditions_p,
	    std::vector<LogicalType> probe_types, std::vector<LogicalType> build_types,
	    std::vector<std::string> names);

	//! One row of the build side, with its join key pre-extracted
	struct BuildRow {
		std::vector<Value> key;
		std::vector<Value> row;
	};

	// --- sink interface (build side) ---
	std::unique_ptr<GlobalSinkState> GetGlobalSinkState(ExecutionContext &context) override;
	std::unique_ptr<LocalSinkState> GetLocalSinkState(ExecutionContext &context, GlobalSinkState &gstate) override;
	void Sink(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate, DataChunk &chunk) override;
	void Combine(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate) override;
	void Finalize(ExecutionContext &context, GlobalSinkState &gstate) override;

	// --- operator interface (probe side) ---
	std::unique_ptr<OperatorState> GetOperatorState(ExecutionContext &context) override;
	void Execute(ExecutionContext &context, DataChunk &chunk, OperatorState &state) override;

	//! (probe-side expression, build-side expression)
	std::vector<std::pair<std::unique_ptr<BoundExpression>, std::unique_ptr<BoundExpression>>> conditions;
	std::vector<LogicalType> probe_types;
	std::vector<LogicalType> build_types;

private:
	std::vector<BuildRow> build_rows_;
	std::unordered_map<std::vector<Value>, std::vector<idx_t>, VectorValueHash, VectorValueEqual> hash_table_;
};

} // namespace tiny_duckdb
