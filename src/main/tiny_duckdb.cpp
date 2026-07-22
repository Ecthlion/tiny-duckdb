#include "tiny_duckdb/main/tiny_duckdb.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "tiny_duckdb/binder/binder.hpp"
#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/execution/executor.hpp"
#include "tiny_duckdb/execution/operator/physical_result_collector.hpp"
#include "tiny_duckdb/execution/physical_plan_generator.hpp"
#include "tiny_duckdb/parser/parser.hpp"

namespace tiny_duckdb {

// ---------------------------------------------------------------------------
// QueryResult
// ---------------------------------------------------------------------------

QueryResult::QueryResult(std::vector<std::string> names_p, std::vector<LogicalType> types_p)
    : names_(std::move(names_p)), types_(std::move(types_p)) {
}

const std::vector<std::string> &QueryResult::Names() const {
	return names_;
}

const std::vector<LogicalType> &QueryResult::Types() const {
	return types_;
}

idx_t QueryResult::RowCount() const {
	idx_t count = 0;
	for (const auto &chunk : chunks_) {
		count += chunk->size();
	}
	return count;
}

Value QueryResult::GetValue(idx_t column, idx_t row) const {
	for (const auto &chunk : chunks_) {
		if (row < chunk->size()) {
			return chunk->GetValue(column, row);
		}
		row -= chunk->size();
	}
	throw Exception("QueryResult: row index out of range");
}

std::vector<std::vector<Value>> QueryResult::ToRows() const {
	std::vector<std::vector<Value>> rows;
	for (const auto &chunk : chunks_) {
		for (idx_t row = 0; row < chunk->size(); row++) {
			std::vector<Value> values;
			for (idx_t col = 0; col < chunk->ColumnCount(); col++) {
				values.push_back(chunk->GetValue(col, row));
			}
			rows.push_back(std::move(values));
		}
	}
	return rows;
}

std::string QueryResult::ToString() const {
	auto rows = ToRows();
	std::vector<idx_t> widths(names_.size(), 0);
	for (idx_t col = 0; col < names_.size(); col++) {
		widths[col] = names_[col].size();
	}
	for (const auto &row : rows) {
		for (idx_t col = 0; col < row.size(); col++) {
			widths[col] = std::max(widths[col], row[col].ToString().size());
		}
	}
	std::ostringstream out;
	for (idx_t col = 0; col < names_.size(); col++) {
		out << (col == 0 ? "| " : " | ") << std::left << std::setw(static_cast<int>(widths[col])) << names_[col];
	}
	out << " |\n";
	for (idx_t col = 0; col < names_.size(); col++) {
		out << "|-" << std::string(widths[col], '-') << '-';
	}
	out << "|\n";
	for (const auto &row : rows) {
		for (idx_t col = 0; col < row.size(); col++) {
			out << (col == 0 ? "| " : " | ") << std::left << std::setw(static_cast<int>(widths[col]))
			    << row[col].ToString();
		}
		out << " |\n";
	}
	out << "(" << rows.size() << " rows)";
	return out.str();
}

void QueryResult::AddChunk(std::unique_ptr<DataChunk> chunk) {
	chunks_.push_back(std::move(chunk));
}

// ---------------------------------------------------------------------------
// TinyDuckDB
// ---------------------------------------------------------------------------

Catalog &TinyDuckDB::GetCatalog() {
	return catalog_;
}

void TinyDuckDB::SetThreads(idx_t threads) {
	threads_ = std::max<idx_t>(threads, 1);
}

idx_t TinyDuckDB::GetThreads() const {
	return threads_.load();
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

Connection::Connection(TinyDuckDB &db) : db_(db) {
}

static std::unique_ptr<QueryResult> OkResult() {
	auto result = std::make_unique<QueryResult>(std::vector<std::string> {"ok"},
	                                       std::vector<LogicalType> {LogicalType::Varchar()});
	auto chunk = std::make_unique<DataChunk>();
	chunk->Initialize({LogicalType::Varchar()});
	chunk->AppendRow({Value::Varchar("OK")});
	result->AddChunk(std::move(chunk));
	return result;
}

std::unique_ptr<QueryResult> Connection::Query(const std::string &sql) {
	SqlParser parser;
	auto statement = parser.Parse(sql);
	Binder binder(db_.GetCatalog());
	auto bound = binder.Bind(*statement);
	switch (bound->type) {
	case StatementType::CREATE_TABLE_STATEMENT: {
		std::vector<std::string> names;
		std::vector<LogicalType> types;
		for (const auto &column : bound->columns) {
			names.push_back(column.name);
			types.push_back(column.type);
		}
		db_.GetCatalog().CreateTable(bound->table_name, names, types);
		return OkResult();
	}
	case StatementType::INSERT_STATEMENT: {
		DataChunk chunk;
		chunk.Initialize(bound->insert_table->GetColumnTypes());
		for (const auto &row : bound->rows) {
			chunk.AppendRow(row);
			if (chunk.size() == STANDARD_VECTOR_SIZE) {
				bound->insert_table->Append(chunk);
				chunk.Reset();
			}
		}
		if (chunk.size() > 0) {
			bound->insert_table->Append(chunk);
		}
		return OkResult();
	}
	case StatementType::SELECT_STATEMENT: {
		PhysicalPlanGenerator generator;
		auto plan = generator.CreatePlan(*bound->plan);
		auto result = std::make_unique<QueryResult>(bound->names, bound->types);
		auto collector = std::make_unique<PhysicalResultCollector>(*result, bound->types, bound->names);
		collector->children.push_back(std::move(plan));
		Executor executor;
		executor.Execute(*collector, db_);
		return result;
	}
	default:
		throw Exception("unknown statement type");
	}
}

} // namespace tiny_duckdb
