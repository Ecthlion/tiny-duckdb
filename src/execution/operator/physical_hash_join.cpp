#include "tiny_duckdb/execution/operator/physical_hash_join.hpp"

#include "tiny_duckdb/execution/expression_executor.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 3 - TASK #5: the hash join (PhysicalHashJoin)
//!
//! The join plays BOTH pipeline roles. Its right (build) child drains into a
//! dedicated pipeline whose SINK is the join itself; only after Finalize does
//! the left (probe) pipeline start, where the join acts as a mid-pipeline
//! OPERATOR. Look at PipelineBuilder (pipeline.cpp) to see this split.
//!
//! Output schema: all probe (left) columns, then all build (right) columns.
//! SQL semantics: a NULL join key NEVER matches (on either side).
//!
//! ----------------------------------------------------------------------------
//! Task L3.T5a - the build side (Sink / Combine / Finalize)
//!
//!   Sink(chunk):     evaluate the build key expressions (condition.second)
//!                    vector-wise, then append one BuildRow {key, row} per
//!                    input row to the thread-local list. Skip rows whose key
//!                    contains NULL.
//!   Combine:         move the thread-local rows into the global list
//!                    (under global.lock).
//!   Finalize:        build the hash table: hash_table_[key] -> list of row
//!                    indexes into build_rows_. (Duplicates matter: one key
//!                    can own many rows.)
//!
//! Hint: BuildRow and the hash table type are declared in the header
//!       (physical_hash_join.hpp). KeyHasNull is provided below.
//!
//! ----------------------------------------------------------------------------
//! Task L3.T5b - the probe side (Execute)
//!
//! For each input chunk evaluate the probe keys (condition.first), look each
//! key up in the hash table and emit every (probe row, matching build row)
//! combination into the output.
//!
//! THE TRICKY PART: one probe row can match MANY build rows, so one input
//! chunk can produce more than STANDARD_VECTOR_SIZE output rows. Execute
//! must emit at most STANDARD_VECTOR_SIZE values per call and RESUME where
//! it stopped on the next call - the provided HashJoinProbeState keeps the
//! pending input, the row cursor and the match cursor for exactly this.
//! Return OperatorResultType::HAVE_MORE_OUTPUT while un-emitted matches
//! remain (the pipeline then calls Execute again WITHOUT pulling a new
//! source chunk), and OperatorResultType::NEED_MORE_INPUT once the pending
//! input is fully consumed.
//!
//! Hint: structure it as two nested loops: outer over pending_input rows,
//!       inner over current_matches; both bounded by STANDARD_VECTOR_SIZE.
//! Hint: NULL probe keys never match - skip the lookup, not the row cursor.
//!
//! Tests: Lab3ExecutionTest.Join*
//! ============================================================================

size_t VectorValueHash::operator()(const std::vector<Value>& key) const {
	uint64_t hash = 0;
	for (const auto& value : key) {
		hash = hash * 31 + value.Hash();
	}
	return static_cast<size_t>(hash);
}

bool VectorValueEqual::operator()(const std::vector<Value>& left, const std::vector<Value>& right) const {
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
bool KeyHasNull(const std::vector<Value>& key) {
	for (const auto& value : key) {
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
	: PhysicalOperator(PhysicalOperatorType::HASH_JOIN,
					   [&] {
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

std::unique_ptr<GlobalSinkState> PhysicalHashJoin::GetGlobalSinkState(ExecutionContext& /*context*/) {
	return std::make_unique<HashJoinGlobalSinkState>();
}

std::unique_ptr<LocalSinkState> PhysicalHashJoin::GetLocalSinkState(ExecutionContext& /*context*/,
																	GlobalSinkState& /*gstate*/) {
	return std::make_unique<HashJoinLocalSinkState>();
}

void PhysicalHashJoin::Sink(ExecutionContext& /*context*/, GlobalSinkState& /*gstate*/, LocalSinkState& lstate,
							DataChunk& chunk) {
	// TODO(L3.T5): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T5 not implemented yet");
}

void PhysicalHashJoin::Combine(ExecutionContext& /*context*/, GlobalSinkState& gstate, LocalSinkState& lstate) {
	// TODO(L3.T5): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T5 not implemented yet");
}

void PhysicalHashJoin::Finalize(ExecutionContext& /*context*/, GlobalSinkState& gstate) {
	// TODO(L3.T5): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T5 not implemented yet");
}

// ---------------------------------------------------------------------------
// PROBE side (operator): match incoming chunks against the hash table
// ---------------------------------------------------------------------------

std::unique_ptr<OperatorState> PhysicalHashJoin::GetOperatorState(ExecutionContext& /*context*/) {
	auto result = std::make_unique<HashJoinProbeState>();
	result->pending_input.Initialize(probe_types);
	result->output.Initialize(types);
	return result;
}

OperatorResultType PhysicalHashJoin::Execute(ExecutionContext& /*context*/, DataChunk& chunk, OperatorState& state) {
	// TODO(L3.T5): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T5 not implemented yet");
}

} // namespace tiny_duckdb
