#pragma once

#include "tiny_duckdb/common/types.hpp"

namespace tiny_duckdb {

class TinyDuckDB;

//! Per-thread execution context handed to every operator call
class ExecutionContext {
public:
	ExecutionContext(TinyDuckDB &db_p, idx_t thread_id_p) : db(db_p), thread_id(thread_id_p) {
	}

	TinyDuckDB &db;
	idx_t thread_id;
};

} // namespace tiny_duckdb
