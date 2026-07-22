#pragma once

#include <memory>
#include <vector>

#include "tiny_duckdb/execution/physical_operator.hpp"

namespace tiny_duckdb {

class TinyDuckDB;

//! ============================================================================
//! LAB 3 - the pipeline: source -> operators -> sink
//!
//! A pipeline is the unit of scheduling. Pipelines that produce data for a
//! sink (the build side of a join, the input of an aggregation/order/limit)
//! are executed first, one after another; threads cooperate inside a single
//! pipeline via morsels.
//! ============================================================================
class Pipeline {
public:
	void Execute(TinyDuckDB &db, idx_t thread_count);

	PhysicalOperator *source = nullptr;
	std::vector<PhysicalOperator *> operators;
	PhysicalOperator *sink = nullptr;

private:
	void ExecuteWorker(ExecutionContext &context, GlobalSourceState *global_source,
	                   GlobalSinkState *global_sink);
};

//! Splits a physical plan tree into pipelines in dependency order.
class PipelineBuilder {
public:
	//! Build all pipelines for the plan rooted at `root` (the result
	//! collector). Returned in execution order: dependencies first.
	std::vector<std::unique_ptr<Pipeline>> Build(PhysicalOperator &root);

private:
	void BuildRecursive(PhysicalOperator &op, Pipeline &current);

	std::vector<std::unique_ptr<Pipeline>> pipelines_;
};

} // namespace tiny_duckdb
