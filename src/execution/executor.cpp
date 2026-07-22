#include "tiny_duckdb/execution/executor.hpp"

#include "tiny_duckdb/main/tiny_duckdb.hpp"

namespace tiny_duckdb {

void Executor::Execute(PhysicalOperator &root, TinyDuckDB &db) {
	PipelineBuilder builder;
	auto pipelines = builder.Build(root);
	for (const auto &pipeline : pipelines) {
		pipeline->Execute(db, db.GetThreads());
	}
}

} // namespace tiny_duckdb
