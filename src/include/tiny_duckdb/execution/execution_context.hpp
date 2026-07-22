#pragma once

#include "tiny_duckdb/common/types.hpp"

namespace tiny_duckdb {

class TinyDuckDB;

//! Per-query execution context: access to the database instance + thread id.
class ExecutionContext {
public:
	ExecutionContext(TinyDuckDB &db, idx_t thread_id) : db(db), thread_id(thread_id) {
	}

	TinyDuckDB &db;
	idx_t thread_id;
};

} // namespace tiny_duckdb
