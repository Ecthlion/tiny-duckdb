#pragma once

#include "tiny_duckdb/execution/pipeline.hpp"

namespace tiny_duckdb {

class TinyDuckDB;

//! Runs a physical plan: builds the pipelines and executes them in
//! dependency order (build/sink pipelines first).
class Executor {
public:
	void Execute(PhysicalOperator &root, TinyDuckDB &db);
};

} // namespace tiny_duckdb
