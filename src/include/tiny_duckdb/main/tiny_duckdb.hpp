#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/common/value.hpp"
#include "tiny_duckdb/storage/catalog.hpp"

namespace tiny_duckdb {

//! The result of a query: a schema plus a list of chunks (DuckDB-style
//! result chunking, so huge results never live in one flat array).
class QueryResult {
public:
	QueryResult(std::vector<std::string> names, std::vector<LogicalType> types);

	const std::vector<std::string> &Names() const;
	const std::vector<LogicalType> &Types() const;
	idx_t RowCount() const;
	Value GetValue(idx_t column, idx_t row) const;
	std::vector<std::vector<Value>> ToRows() const;
	std::string ToString() const;

	void AddChunk(std::unique_ptr<DataChunk> chunk);

private:
	std::vector<std::string> names_;
	std::vector<LogicalType> types_;
	std::vector<std::unique_ptr<DataChunk>> chunks_;
};

//! The database instance: owns the catalog and the parallelism setting.
class TinyDuckDB {
public:
	Catalog &GetCatalog();

	void SetThreads(idx_t threads);
	idx_t GetThreads() const;

private:
	Catalog catalog_;
	std::atomic<idx_t> threads_ {4};
};

//! A connection issues queries against a database instance.
class Connection {
public:
	explicit Connection(TinyDuckDB &db);

	//! Parse -> bind -> plan -> execute one SQL statement
	std::unique_ptr<QueryResult> Query(const std::string &sql);

private:
	TinyDuckDB &db_;
};

} // namespace tiny_duckdb
