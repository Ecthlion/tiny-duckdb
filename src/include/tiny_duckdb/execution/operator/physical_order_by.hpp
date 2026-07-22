#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/execution/physical_operator.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 3 (Task L3.T6) - ORDER BY and LIMIT
//!
//! Both are SINK + SOURCE: they must see all input before producing output.
//! Rows are materialized as vector<Value> (simple, if not cache-optimal),
//! then emitted through an atomic offset so multiple threads can drain.
//! ============================================================================

//! Sinks all rows, stable-sorts them in Finalize, emits them in GetData.
class PhysicalOrderBy : public PhysicalOperator {
public:
	PhysicalOrderBy(std::vector<std::pair<idx_t, bool>> keys_p, std::vector<LogicalType> types_p,
	                std::vector<std::string> names_p);

	std::unique_ptr<GlobalSinkState> GetGlobalSinkState(ExecutionContext &context) override;
	std::unique_ptr<LocalSinkState> GetLocalSinkState(ExecutionContext &context, GlobalSinkState &gstate) override;
	void Sink(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate, DataChunk &chunk) override;
	void Combine(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate) override;
	void Finalize(ExecutionContext &context, GlobalSinkState &gstate) override;

	void GetData(ExecutionContext &context, DataChunk &chunk, SourceInput &input) override;

	std::vector<std::pair<idx_t, bool>> keys;

private:
	std::vector<std::vector<Value>> result_rows_;
	std::atomic<idx_t> emit_offset_ {0};
};

//! Sinks rows until the limit is reached, then emits them.
class PhysicalLimit : public PhysicalOperator {
public:
	PhysicalLimit(int64_t limit_p, std::vector<LogicalType> types_p, std::vector<std::string> names_p);

	std::unique_ptr<GlobalSinkState> GetGlobalSinkState(ExecutionContext &context) override;
	std::unique_ptr<LocalSinkState> GetLocalSinkState(ExecutionContext &context, GlobalSinkState &gstate) override;
	void Sink(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate, DataChunk &chunk) override;
	void Combine(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate) override;
	void Finalize(ExecutionContext &context, GlobalSinkState &gstate) override;

	void GetData(ExecutionContext &context, DataChunk &chunk, SourceInput &input) override;

	int64_t limit;

private:
	std::vector<std::vector<Value>> result_rows_;
	std::atomic<idx_t> emit_offset_ {0};
};

} // namespace tiny_duckdb
