#include "tiny_duckdb/execution/operator/physical_table_scan.hpp"

#include "tiny_duckdb/execution/execution_context.hpp"
#include "tiny_duckdb/storage/table_data.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 3 - TASK #3: the morsel-driven table scan (PhysicalTableScan)
//!
//! The table scan is the SOURCE of every SELECT pipeline. Many worker threads
//! call GetData() concurrently; the shared ParallelTableScanState (stored in
//! the GlobalSourceState) hands each caller the next morsel - a
//! STANDARD_VECTOR_SIZE slice of one row group (Lab 1 built this).
//!
//! ----------------------------------------------------------------------------
//! Task L3.T3 - PhysicalTableScan::GetData
//!
//! Produce the next chunk of the table into `chunk`:
//!   1. Grab the next morsel from the shared scan state (NextMorsel). When
//!      the scan is exhausted, emit an EMPTY chunk (chunk.SetCardinality(0))
//!      - that is the pipeline's end-of-stream signal.
//!   2. ZONE MAP PRUNING: before materializing a morsel, check every
//!      TableFilter against the row group's zone map via
//!      TableData::CheckZoneMap(row_group_index, column_id, constant,
//!      comparison). If ANY filter proves the whole row group cannot match,
//!      skip the morsel entirely and grab the next one. This is what makes
//!      analytic scans skip 99% of the data - never materialize a chunk you
//!      can prove is useless.
//!   3. Otherwise call TableData::Scan(morsel, column_ids, chunk) and return.
//!
//! A TableFilter is a predicate `column <comparison> constant` extracted from
//! the WHERE clause by the plan generator; the remaining (non-pushable)
//! predicates are still evaluated by the PhysicalFilter downstream - pruning
//! must never change the query RESULT, only the amount of data read.
//!
//! Hint: the loop structure is "grab morsel -> maybe prune -> scan or
//!       continue". Think about what happens when EVERY morsel is pruned.
//! Hint: TableData::CheckZoneMap returns false when the comparison is
//!       IMPOSSIBLE for the row group (e.g. col > 100 while max is 99).
//!
//! Tests: Lab3ExecutionTest.ZoneMapPrunedScanStillCorrect /
//!        ParallelScanConsistency / ScanEmptyTable
//! ============================================================================

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

std::unique_ptr<GlobalSourceState> PhysicalTableScan::GetGlobalSourceState(ExecutionContext & /*context*/) {
	auto result = std::make_unique<TableScanGlobalSourceState>();
	result->scan_state = table.CreateParallelScanState();
	return result;
}

void PhysicalTableScan::GetData(ExecutionContext & /*context*/, DataChunk &chunk, SourceInput &input) {
	// [SOLUTION BEGIN L3.T3]
	auto &gstate = input.global_state->Cast<TableScanGlobalSourceState>();
	TableScanMorsel morsel;
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
	// [SOLUTION END]
}

} // namespace tiny_duckdb
