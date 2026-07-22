#include "tiny_duckdb/execution/operator/physical_order_by.hpp"

#include <algorithm>

namespace tiny_duckdb {

namespace {

//! Shared materialization state used by both ORDER BY and LIMIT
class MaterializeGlobalSinkState : public GlobalSinkState {
public:
	std::vector<std::vector<Value>> rows;
	std::mutex lock;
};

class MaterializeLocalSinkState : public LocalSinkState {
public:
	std::vector<std::vector<Value>> rows;
};

void MaterializeChunk(DataChunk &chunk, std::vector<std::vector<Value>> &rows) {
	for (idx_t row = 0; row < chunk.size(); row++) {
		std::vector<Value> values;
		for (idx_t col = 0; col < chunk.ColumnCount(); col++) {
			values.push_back(chunk.GetValue(col, row));
		}
		rows.push_back(std::move(values));
	}
}

void CombineRows(MaterializeGlobalSinkState &global, MaterializeLocalSinkState &local) {
	std::lock_guard<std::mutex> guard(global.lock);
	for (auto &row : local.rows) {
		global.rows.push_back(std::move(row));
	}
	local.rows.clear();
}

idx_t EmitRows(const std::vector<std::vector<Value>> &rows, std::atomic<idx_t> &emit_offset, DataChunk &chunk) {
	idx_t offset = emit_offset.fetch_add(STANDARD_VECTOR_SIZE);
	if (offset >= rows.size()) {
		chunk.SetCardinality(0);
		return 0;
	}
	idx_t count = std::min<idx_t>(STANDARD_VECTOR_SIZE, rows.size() - offset);
	for (idx_t row = 0; row < count; row++) {
		for (idx_t col = 0; col < rows[offset + row].size(); col++) {
			chunk.SetValue(col, row, rows[offset + row][col]);
		}
	}
	chunk.SetCardinality(count);
	return count;
}

} // namespace

// ---------------------------------------------------------------------------
// ORDER BY
// ---------------------------------------------------------------------------

PhysicalOrderBy::PhysicalOrderBy(std::vector<std::pair<idx_t, bool>> keys_p, std::vector<LogicalType> types_p,
                                 std::vector<std::string> names_p)
    : PhysicalOperator(PhysicalOperatorType::ORDER_BY, std::move(types_p)), keys(std::move(keys_p)) {
	names = std::move(names_p);
}

std::unique_ptr<GlobalSinkState> PhysicalOrderBy::GetGlobalSinkState(ExecutionContext &/*context*/) {
	return std::make_unique<MaterializeGlobalSinkState>();
}

std::unique_ptr<LocalSinkState> PhysicalOrderBy::GetLocalSinkState(ExecutionContext &/*context*/,
                                                                   GlobalSinkState &/*gstate*/) {
	return std::make_unique<MaterializeLocalSinkState>();
}

void PhysicalOrderBy::Sink(ExecutionContext &/*context*/, GlobalSinkState &/*gstate*/, LocalSinkState &lstate,
                           DataChunk &chunk) {
	MaterializeChunk(chunk, lstate.Cast<MaterializeLocalSinkState>().rows);
}

void PhysicalOrderBy::Combine(ExecutionContext &/*context*/, GlobalSinkState &gstate, LocalSinkState &lstate) {
	CombineRows(gstate.Cast<MaterializeGlobalSinkState>(), lstate.Cast<MaterializeLocalSinkState>());
}

void PhysicalOrderBy::Finalize(ExecutionContext &/*context*/, GlobalSinkState &gstate) {
	auto &global = gstate.Cast<MaterializeGlobalSinkState>();
	result_rows_ = std::move(global.rows);
	// stable multi-key sort; NULLs sort first (Value::LessThan)
	std::stable_sort(result_rows_.begin(), result_rows_.end(), [this](const std::vector<Value> &left,
	                                                                  const std::vector<Value> &right) {
		for (const auto &key : keys) {
			const auto &lval = left[key.first];
			const auto &rval = right[key.first];
			if (Value::Equals(lval, rval)) {
				continue;
			}
			if (key.second) {
				return Value::LessThan(lval, rval);
			}
			return Value::LessThan(rval, lval);
		}
		return false;
	});
}

void PhysicalOrderBy::GetData(ExecutionContext &/*context*/, DataChunk &chunk, SourceInput &/*input*/) {
	EmitRows(result_rows_, emit_offset_, chunk);
}

// ---------------------------------------------------------------------------
// LIMIT
// ---------------------------------------------------------------------------

PhysicalLimit::PhysicalLimit(int64_t limit_p, std::vector<LogicalType> types_p, std::vector<std::string> names_p)
    : PhysicalOperator(PhysicalOperatorType::LIMIT, std::move(types_p)), limit(limit_p) {
	names = std::move(names_p);
}

std::unique_ptr<GlobalSinkState> PhysicalLimit::GetGlobalSinkState(ExecutionContext &/*context*/) {
	return std::make_unique<MaterializeGlobalSinkState>();
}

std::unique_ptr<LocalSinkState> PhysicalLimit::GetLocalSinkState(ExecutionContext &/*context*/,
                                                                 GlobalSinkState &/*gstate*/) {
	return std::make_unique<MaterializeLocalSinkState>();
}

void PhysicalLimit::Sink(ExecutionContext &/*context*/, GlobalSinkState &gstate, LocalSinkState &/*lstate*/,
                         DataChunk &chunk) {
	// limit is not order-sensitive: truncate globally under the lock
	auto &global = gstate.Cast<MaterializeGlobalSinkState>();
	std::lock_guard<std::mutex> guard(global.lock);
	idx_t remaining = global.rows.size() >= static_cast<idx_t>(limit)
	                      ? 0
	                      : static_cast<idx_t>(limit) - global.rows.size();
	if (remaining == 0) {
		return;
	}
	idx_t count = std::min<idx_t>(chunk.size(), remaining);
	for (idx_t row = 0; row < count; row++) {
		std::vector<Value> values;
		for (idx_t col = 0; col < chunk.ColumnCount(); col++) {
			values.push_back(chunk.GetValue(col, row));
		}
		global.rows.push_back(std::move(values));
	}
}

void PhysicalLimit::Combine(ExecutionContext &/*context*/, GlobalSinkState &/*gstate*/, LocalSinkState &/*lstate*/) {
	// Sink wrote straight into the global state; nothing to combine
}

void PhysicalLimit::Finalize(ExecutionContext &/*context*/, GlobalSinkState &gstate) {
	auto &global = gstate.Cast<MaterializeGlobalSinkState>();
	result_rows_ = std::move(global.rows);
}

void PhysicalLimit::GetData(ExecutionContext &/*context*/, DataChunk &chunk, SourceInput &/*input*/) {
	EmitRows(result_rows_, emit_offset_, chunk);
}

} // namespace tiny_duckdb
