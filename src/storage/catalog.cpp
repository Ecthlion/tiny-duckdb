#include "tiny_duckdb/storage/catalog.hpp"

#include <algorithm>
#include <cctype>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

std::string Catalog::NormalizeName(const std::string &name) {
	std::string result = name;
	std::transform(result.begin(), result.end(), result.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

void Catalog::CreateTable(const std::string &name, const std::vector<std::string> &column_names,
                          const std::vector<LogicalType> &column_types) {
	std::lock_guard<std::mutex> guard(lock_);
	const std::string key = NormalizeName(name);
	if (tables_.count(key) != 0) {
		throw CatalogException("Table already exists: " + name);
	}
	std::vector<std::string> normalized_columns;
	normalized_columns.reserve(column_names.size());
	for (const auto &column : column_names) {
		normalized_columns.push_back(NormalizeName(column));
	}
	tables_[key] = std::make_unique<TableData>(key, normalized_columns, column_types);
}

TableData &Catalog::GetTable(const std::string &name) const {
	std::lock_guard<std::mutex> guard(lock_);
	const auto entry = tables_.find(NormalizeName(name));
	if (entry == tables_.end()) {
		throw CatalogException("Table does not exist: " + name);
	}
	return *entry->second;
}

bool Catalog::TableExists(const std::string &name) const {
	std::lock_guard<std::mutex> guard(lock_);
	return tables_.count(NormalizeName(name)) != 0;
}

std::vector<std::string> Catalog::ListTables() const {
	std::lock_guard<std::mutex> guard(lock_);
	std::vector<std::string> result;
	for (const auto &entry : tables_) {
		result.push_back(entry.first);
	}
	return result;
}

} // namespace tiny_duckdb
