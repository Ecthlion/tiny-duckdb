#include "tiny_duckdb/execution/operator/physical_hash_join.hpp"

#include "tiny_duckdb/execution/expression_executor.hpp"

namespace tiny_duckdb {

size_t VectorValueHash::operator()(const std::vector<Value> &key) const {
	uint64_t hash = 0;
	for (const auto &value : key) {
		hash = hash * 31 + value.Hash();
	}
	return static_cast<size_t>(hash);
}

bool VectorValueEqual::operator()(const std::vector<Value> &left, const std::vector<Value> &right) const {
	if (left.size() != right.size()) {
		return false;
	}
	for (idx_t i = 0; i < left.size(); i++) {
		if (!Value::Equals(left[i], right[i])) {
			return false;
		}
	}
	return true;
}

namespace {

//! SQL semantics: a NULL join key never matches anything
bool KeyHasNull(const std::vector<Value> &key) {
	for (const auto &value : key) {
		if (value.IsNull()) {
			return true;
		}
	}
	return false;
}

class HashJoinGlobalSinkState : public GlobalSinkState {
public:
	std::vector<PhysicalHashJoin::BuildRow> rows;
	std::mutex lock;
};

class HashJoinLocalSinkState : public LocalSinkState {
public:
	std::vector<PhysicalHashJoin::BuildRow> rows;
};

class HashJoinProbeState : public OperatorState {
public:
	DataChunk pending_input;
	std::vector<std::vector<Value>> pending_keys;
	bool has_pending = false;
	idx_t row_cursor = 0;
	std::vector<idx_t> current_matches;
	idx_t match_cursor = 0;
	DataChunk output;
};

} // namespace

PhysicalHashJoin::PhysicalHashJoin(
    std::vector<std::pair<std::unique_ptr<BoundExpression>, std::unique_ptr<BoundExpression>>> conditions_p,
    std::vector<LogicalType> probe_types_p, std::vector<LogicalType> build_types_p, std::vector<std::string> names_p)
    : PhysicalOperator(PhysicalOperatorType::HASH_JOIN, [&] {
	      std::vector<LogicalType> output = probe_types_p;
	      output.insert(output.end(), build_types_p.begin(), build_types_p.end());
	      return output;
	  }()),
      conditions(std::move(conditions_p)), probe_types(std::move(probe_types_p)),
      build_types(std::move(build_types_p)) {
	names = std::move(names_p);
}

// ---------------------------------------------------------------------------
// BUILD side (sink): materialize the right child into a hash table
// ---------------------------------------------------------------------------

std::unique_ptr<GlobalSinkState> PhysicalHashJoin::GetGlobalSinkState(ExecutionContext &/*context*/) {
	return std::make_unique<HashJoinGlobalSinkState>();
}

std::unique_ptr<LocalSinkState> PhysicalHashJoin::GetLocalSinkState(ExecutionContext &/*context*/,
                                                                    GlobalSinkState &/*gstate*/) {
	return std::make_unique<HashJoinLocalSinkState>();
}

void PhysicalHashJoin::Sink(ExecutionContext &/*context*/, GlobalSinkState &/*gstate*/, LocalSinkState &lstate,
                            DataChunk &chunk) {
	auto &local = lstate.Cast<HashJoinLocalSinkState>();
	// evaluate the build-side keys once per chunk (vectorized)
	std::vector<std::unique_ptr<Vector>> key_vectors;
	for (const auto &condition : conditions) {
		auto vector = std::make_unique<Vector>(condition.second->return_type);
		ExpressionExecutor::Evaluate(*condition.second, chunk, *vector);
		key_vectors.push_back(std::move(vector));
	}
	for (idx_t row = 0; row < chunk.size(); row++) {
		BuildRow build_row;
		for (const auto &vector : key_vectors) {
			build_row.key.push_back(vector->GetValue(row));
		}
		if (KeyHasNull(build_row.key)) {
			continue;
		}
		for (idx_t col = 0; col < chunk.ColumnCount(); col++) {
			build_row.row.push_back(chunk.GetValue(col, row));
		}
		local.rows.push_back(std::move(build_row));
	}
}

void PhysicalHashJoin::Combine(ExecutionContext &/*context*/, GlobalSinkState &gstate, LocalSinkState &lstate) {
	auto &global = gstate.Cast<HashJoinGlobalSinkState>();
	auto &local = lstate.Cast<HashJoinLocalSinkState>();
	std::lock_guard<std::mutex> guard(global.lock);
	for (auto &row : local.rows) {
		global.rows.push_back(std::move(row));
	}
	local.rows.clear();
}

void PhysicalHashJoin::Finalize(ExecutionContext &/*context*/, GlobalSinkState &gstate) {
	auto &global = gstate.Cast<HashJoinGlobalSinkState>();
	build_rows_ = std::move(global.rows);
	for (idx_t i = 0; i < build_rows_.size(); i++) {
		hash_table_[build_rows_[i].key].push_back(i);
	}
}

// ---------------------------------------------------------------------------
// PROBE side (operator): match incoming chunks against the hash table
// ---------------------------------------------------------------------------

std::unique_ptr<OperatorState> PhysicalHashJoin::GetOperatorState(ExecutionContext &/*context*/) {
	auto result = std::make_unique<HashJoinProbeState>();
	result->pending_input.Initialize(probe_types);
	result->output.Initialize(types);
	return result;
}

void PhysicalHashJoin::Execute(ExecutionContext &/*context*/, DataChunk &chunk, OperatorState &state) {
	auto &probe = state.Cast<HashJoinProbeState>();
	if (!probe.has_pending) {
		// new input chunk: stash it and pre-compute the probe keys
		probe.pending_input.CopyFrom(chunk);
		std::vector<std::unique_ptr<Vector>> key_vectors;
		for (const auto &condition : conditions) {
			auto vector = std::make_unique<Vector>(condition.first->return_type);
			ExpressionExecutor::Evaluate(*condition.first, chunk, *vector);
			key_vectors.push_back(std::move(vector));
		}
		probe.pending_keys.clear();
		for (idx_t row = 0; row < chunk.size(); row++) {
			std::vector<Value> key;
			for (const auto &vector : key_vectors) {
				key.push_back(vector->GetValue(row));
			}
			probe.pending_keys.push_back(std::move(key));
		}
		probe.has_pending = true;
		probe.row_cursor = 0;
		probe.match_cursor = 0;
	}
	// emit matches; may resume mid-row from a previous (full) call
	idx_t output_count = 0;
	while (probe.row_cursor < probe.pending_input.size() && output_count < STANDARD_VECTOR_SIZE) {
		if (probe.match_cursor == 0) {
			probe.current_matches.clear();
			if (!KeyHasNull(probe.pending_keys[probe.row_cursor])) {
				auto entry = hash_table_.find(probe.pending_keys[probe.row_cursor]);
				if (entry != hash_table_.end()) {
					probe.current_matches = entry->second;
				}
			}
		}
		while (probe.match_cursor < probe.current_matches.size() && output_count < STANDARD_VECTOR_SIZE) {
			auto &build_row = build_rows_[probe.current_matches[probe.match_cursor]];
			for (idx_t col = 0; col < probe_types.size(); col++) {
				probe.output.SetValue(col, output_count, probe.pending_input.GetValue(col, probe.row_cursor));
			}
			for (idx_t col = 0; col < build_types.size(); col++) {
				probe.output.SetValue(probe_types.size() + col, output_count, build_row.row[col]);
			}
			output_count++;
			probe.match_cursor++;
		}
		if (probe.match_cursor == probe.current_matches.size()) {
			probe.row_cursor++;
			probe.match_cursor = 0;
		}
	}
	if (probe.row_cursor == probe.pending_input.size()) {
		probe.has_pending = false;
	}
	probe.output.SetCardinality(output_count);
	chunk = std::move(probe.output);
	probe.output.Initialize(types);
}

} // namespace tiny_duckdb
