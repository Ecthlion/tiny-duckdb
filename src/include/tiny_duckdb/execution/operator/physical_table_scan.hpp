#pragma once

#include <memory>
#include <vector>

#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/value.hpp"
#include "tiny_duckdb/execution/physical_operator.hpp"

namespace tiny_duckdb {

class TableData;
class ParallelTableScanState;

//! A predicate pushed down to the table scan: `column CMP constant`
struct TableFilter {
	idx_t column_id;
	ExpressionType comparison;
	Value constant;
};

//! ============================================================================
//! LAB 3 (Task L3.T3) - the morsel-driven table scan
//!
//! SOURCE only. GetData() asks the shared ParallelTableScanState for the next
//! morsel, checks zone maps to skip it if possible, and otherwise scans it
//! into the chunk. table_filters are predicates pushed down from the WHERE
//! clause (see PhysicalPlanGenerator).
//! ============================================================================
class PhysicalTableScan : public PhysicalOperator {
public:
	PhysicalTableScan(TableData &table, std::vector<idx_t> column_ids, std::vector<LogicalType> types,
	                  std::vector<std::string> names, std::vector<TableFilter> table_filters);

	std::unique_ptr<GlobalSourceState> GetGlobalSourceState(ExecutionContext &context) override;
	void GetData(ExecutionContext &context, DataChunk &chunk, SourceInput &input) override;

	TableData &table;
	std::vector<idx_t> column_ids;
	std::vector<TableFilter> table_filters;
};

} // namespace tiny_duckdb
