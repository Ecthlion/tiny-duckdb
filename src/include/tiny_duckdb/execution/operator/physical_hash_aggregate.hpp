#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "tiny_duckdb/binder/bound_expression.hpp"
#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/execution/physical_operator.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 3 (Task L3.T4) - the parallel hash aggregate
//!
//! SINK + SOURCE. Threads sink chunks into thread-local hash tables (no
//! locking!), Combine() merges them into the global table, Finalize()
//! materializes the result rows, and then GetData() emits them.
//!
//! This two-phase "local then global" scheme is exactly how DuckDB keeps
//! aggregation parallel without contention.
//! ============================================================================
class PhysicalHashAggregate : public PhysicalOperator {
public:
	PhysicalHashAggregate(std::vector<std::unique_ptr<BoundExpression>> groups_p,
	                      std::vector<std::unique_ptr<BoundAggregateExpression>> aggregates_p,
	                      std::vector<LogicalType> types, std::vector<std::string> names);

	// --- sink interface (build the hash table) ---
	std::unique_ptr<GlobalSinkState> GetGlobalSinkState(ExecutionContext &context) override;
	std::unique_ptr<LocalSinkState> GetLocalSinkState(ExecutionContext &context, GlobalSinkState &gstate) override;
	void Sink(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate, DataChunk &chunk) override;
	void Combine(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate) override;
	void Finalize(ExecutionContext &context, GlobalSinkState &gstate) override;

	// --- source interface (emit result rows) ---
	void GetData(ExecutionContext &context, DataChunk &chunk, SourceInput &input) override;

	std::vector<std::unique_ptr<BoundExpression>> groups;
	std::vector<std::unique_ptr<BoundAggregateExpression>> aggregates;

private:
	//! Materialized result rows, filled by Finalize() and emitted by GetData()
	std::vector<std::vector<Value>> result_rows_;
	std::atomic<idx_t> emit_offset_ {0};
};

} // namespace tiny_duckdb
