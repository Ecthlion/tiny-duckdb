#include "tiny_duckdb/execution/pipeline.hpp"

#include <exception>
#include <mutex>
#include <thread>

#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/execution/execution_context.hpp"
#include "tiny_duckdb/main/tiny_duckdb.hpp"

namespace tiny_duckdb {

void Pipeline::Execute(TinyDuckDB &db, idx_t thread_count) {
	if (!source) {
		throw ExecutorException("pipeline without a source");
	}
	ExecutionContext context(db, 0);
	auto global_source = source->GetGlobalSourceState(context);
	std::unique_ptr<GlobalSinkState> global_sink;
	if (sink) {
		global_sink = sink->GetGlobalSinkState(context);
	}
	// main thread + (thread_count - 1) workers, all pulling morsels.
	// Capture worker exceptions and rethrow on the calling thread (DuckDB's
	// Executor does the same): an exception escaping a std::thread - or a
	// main-thread throw while workers are still joinable - is std::terminate.
	std::vector<std::thread> workers;
	std::mutex error_lock;
	std::vector<std::exception_ptr> worker_errors;
	for (idx_t t = 1; t < thread_count; t++) {
		workers.emplace_back([this, &db, t, &global_source, &global_sink, &error_lock, &worker_errors] {
			try {
				ExecutionContext worker_context(db, t);
				ExecuteWorker(worker_context, global_source.get(), global_sink.get());
			} catch (...) {
				std::lock_guard<std::mutex> guard(error_lock);
				worker_errors.push_back(std::current_exception());
			}
		});
	}
	std::exception_ptr main_error;
	try {
		ExecuteWorker(context, global_source.get(), global_sink.get());
	} catch (...) {
		main_error = std::current_exception();
	}
	for (auto &worker : workers) {
		worker.join();
	}
	if (main_error) {
		std::rethrow_exception(main_error);
	}
	if (!worker_errors.empty()) {
		std::rethrow_exception(worker_errors.front());
	}
	if (sink) {
		sink->Finalize(context, *global_sink);
	}
}

void Pipeline::ExecuteWorker(ExecutionContext &context, GlobalSourceState *global_source,
                             GlobalSinkState *global_sink) {
	auto source_state = source->GetOperatorState(context);
	std::vector<std::unique_ptr<OperatorState>> operator_states;
	for (const auto &op : operators) {
		operator_states.push_back(op->GetOperatorState(context));
	}
	std::unique_ptr<LocalSinkState> local_sink;
	if (sink) {
		local_sink = sink->GetLocalSinkState(context, *global_sink);
	}
	DataChunk chunk;
	while (true) {
		// re-initialize every iteration: operators (projection/join) replace
		// the chunk's schema as it flows through the pipeline
		chunk.Initialize(source->types);
		SourceInput input {*source_state, global_source};
		source->GetData(context, chunk, input);
		if (chunk.size() == 0) {
			break;
		}
		bool filtered_out = false;
		for (idx_t i = 0; i < operators.size(); i++) {
			operators[i]->Execute(context, chunk, *operator_states[i]);
			if (chunk.size() == 0) {
				filtered_out = true;
				break;
			}
		}
		if (filtered_out) {
			continue;
		}
		if (sink) {
			sink->Sink(context, *global_sink, *local_sink, chunk);
		}
	}
	if (sink) {
		sink->Combine(context, *global_sink, *local_sink);
	}
}

// ---------------------------------------------------------------------------
// PipelineBuilder
// ---------------------------------------------------------------------------

std::vector<std::unique_ptr<Pipeline>> PipelineBuilder::Build(PhysicalOperator &root) {
	auto current = std::make_unique<Pipeline>();
	BuildRecursive(root, *current);
	pipelines_.push_back(std::move(current));
	return std::move(pipelines_);
}

void PipelineBuilder::BuildRecursive(PhysicalOperator &op, Pipeline &current) {
	switch (op.type) {
	case PhysicalOperatorType::RESULT_COLLECTOR:
		current.sink = &op;
		BuildRecursive(*op.children[0], current);
		return;
	case PhysicalOperatorType::TABLE_SCAN:
		current.source = &op;
		return;
	case PhysicalOperatorType::FILTER:
	case PhysicalOperatorType::PROJECTION:
		BuildRecursive(*op.children[0], current);
		current.operators.push_back(&op);
		return;
	case PhysicalOperatorType::HASH_JOIN: {
		// right child: a dedicated build pipeline whose sink is the join
		auto build_pipeline = std::make_unique<Pipeline>();
		build_pipeline->sink = &op;
		BuildRecursive(*op.children[1], *build_pipeline);
		pipelines_.push_back(std::move(build_pipeline));
		// left child continues the current pipeline; the join probes mid-flow
		BuildRecursive(*op.children[0], current);
		current.operators.push_back(&op);
		return;
	}
	case PhysicalOperatorType::HASH_GROUP_BY:
	case PhysicalOperatorType::ORDER_BY:
	case PhysicalOperatorType::LIMIT: {
		// child: a dedicated pipeline whose sink is this operator; afterwards
		// the operator becomes the source of the current pipeline
		auto child_pipeline = std::make_unique<Pipeline>();
		child_pipeline->sink = &op;
		BuildRecursive(*op.children[0], *child_pipeline);
		pipelines_.push_back(std::move(child_pipeline));
		current.source = &op;
		return;
	}
	default:
		throw ExecutorException("PipelineBuilder: unsupported operator");
	}
}

} // namespace tiny_duckdb
