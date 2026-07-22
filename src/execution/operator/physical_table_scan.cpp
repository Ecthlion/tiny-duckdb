#include "tiny_duckdb/execution/operator/physical_table_scan.hpp"

#include "tiny_duckdb/execution/execution_context.hpp"
#include "tiny_duckdb/storage/table_data.hpp"

namespace tiny_duckdb {

//! Shared between all scan threads: hands out morsels
class TableScanGlobalSourceState : public GlobalSourceState {
public:
	std::unique_ptr<ParallelTableScanState> scan_state;
};

PhysicalTableScan::PhysicalTableScan(TableData &table_p, std::vector<idx_t> column_ids_p,
                                     std::vector<LogicalType> types_p, std::vector<std::string> names_p,
                                     std::vector<TableFilter> table_filters_p)
    : PhysicalOperator(PhysicalOperatorType::TABLE_SCAN, std::move(types_p)), table(table_p),
      column_ids(std::move(column_ids_p)), table_filters(std::move(table_filters_p)) {
	names = std::move(names_p);
}

std::unique_ptr<GlobalSourceState> PhysicalTableScan::GetGlobalSourceState(ExecutionContext &/*context*/) {
	auto result = std::make_unique<TableScanGlobalSourceState>();
	result->scan_state = table.CreateParallelScanState();
	return result;
}

void PhysicalTableScan::GetData(ExecutionContext &/*context*/, DataChunk &chunk, SourceInput &input) {
	auto &gstate = input.global_state->Cast<TableScanGlobalSourceState>();
	TableScanMorsel morsel;
	// ------------------------------------------------------------------
	// L3.T3: morsel-driven scan with zone map pruning.
	// Grab morsels until we find one that no zone map can rule out.
	// ------------------------------------------------------------------
	while (gstate.scan_state->NextMorsel(morsel)) {
		bool pruned = false;
		for (const auto &filter : table_filters) {
			if (!table.CheckZoneMap(morsel.row_group_index, filter.column_id, filter.constant,
			                        filter.comparison)) {
				pruned = true;
				break;
			}
		}
		if (pruned) {
			continue;
		}
		table.Scan(morsel, column_ids, chunk);
		return;
	}
	chunk.SetCardinality(0);
}

} // namespace tiny_duckdb
