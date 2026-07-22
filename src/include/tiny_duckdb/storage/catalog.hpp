#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "tiny_duckdb/common/types.hpp"
#include "tiny_duckdb/storage/table_data.hpp"

namespace tiny_duckdb {

//! The catalog maps table names to tables. Names are case-insensitive
//! (stored lower-case), like DuckDB.
class Catalog {
public:
	//! Create a table; throws CatalogException if it already exists
	void CreateTable(const std::string &name, const std::vector<std::string> &column_names,
	                 const std::vector<LogicalType> &column_types);

	//! Look up a table; throws CatalogException if it does not exist
	TableData &GetTable(const std::string &name) const;

	bool TableExists(const std::string &name) const;
	std::vector<std::string> ListTables() const;

	static std::string NormalizeName(const std::string &name);

private:
	mutable std::mutex lock_;
	std::map<std::string, std::unique_ptr<TableData>> tables_;
};

} // namespace tiny_duckdb
