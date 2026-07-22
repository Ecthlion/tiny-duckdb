#include "tiny_duckdb/planner/logical_plan.hpp"

#include "tiny_duckdb/storage/table_data.hpp"

namespace tiny_duckdb {

LogicalGet::LogicalGet(TableData &table_p, std::vector<idx_t> column_ids_p)
    : LogicalOperator(LogicalOperatorType::LOGICAL_GET), table(table_p), column_ids(std::move(column_ids_p)) {
	for (const idx_t column_id : column_ids) {
		names.push_back(table.GetColumnNames()[column_id]);
		types.push_back(table.GetColumnTypes()[column_id]);
	}
}

} // namespace tiny_duckdb
