#include "tiny_duckdb/execution/operator/physical_hash_aggregate.hpp"

#include <unordered_map>

#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/execution/expression_executor.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 3 - TASK #4: the parallel hash aggregate (PhysicalHashAggregate)
//!
//! GROUP BY is a SINK: it consumes the whole input before producing anything.
//! Parallelism uses DuckDB's two-phase protocol:
//!
//!   phase 1  every worker folds its morsels into a THREAD-LOCAL hash table
//!            (Sink) - no locking at all on the hot path;
//!   phase 2  the per-thread tables are merged into one global table under a
//!            lock (Combine), then each group is finalized into output rows
//!            (Finalize). Afterwards the operator turns into a SOURCE and
//!            hands the result rows to its parent (GetData).
//!
//! ----------------------------------------------------------------------------
//! Task L3.T4a - the aggregate state machine (AggregateState)
//!
//! One AggregateState tracks ONE aggregate function over ONE group:
//!
//!   Update(input): fold one row's argument into the state.
//!     COUNT(*) / COUNT(col)   count++; COUNT ignores NULL arguments
//!     SUM / AVG               accumulate sum and count (skip NULLs)
//!     MIN / MAX               keep the best candidate so far (skip NULLs;
//!                             has_value tracks whether any value was seen)
//!   Merge(other): combine two partial states (used by Combine). count and
//!     sum add up; min/max keep the better candidate.
//!   Finalize(return_type): produce the output Value.
//!     COUNT*                  always Value::BigInt(count) - never NULL
//!     SUM / AVG / MIN / MAX   NULL when no non-NULL input was seen
//!     AVG                     sum / count as DOUBLE
//!
//! Hint: Value::GetNumeric() reads any numeric Value as double.
//! Hint: for SUM, look at return_type.Id(): DOUBLE inputs produce
//!       Value::Double(sum), integers Value::BigInt((int64_t)sum).
//!
//! Tests: Lab3ExecutionTest.CountStar / SumAvgMinMax / Aggregate*Null* /
//!        AggregateEmptyTableNoGroupBy
//!
//! ----------------------------------------------------------------------------
//! Task L3.T4b - the two-phase parallel protocol (Sink/Combine/Finalize/GetData)
//!
//!   Sink(chunk):     evaluate the group-by expressions and the aggregate
//!                    arguments VECTOR-WISE (one ExpressionExecutor::Evaluate
//!                    per expression per chunk), then fold every row into the
//!                    thread-local GroupByHashTable (FindOrCreate + Update).
//!   Combine:         merge the thread-local table into the global one under
//!                    global.lock (FindOrCreate + Merge).
//!   Finalize:        turn every group into one output row
//!                    [group key columns..., aggregate results...].
//!                    SPECIAL CASE: aggregation WITHOUT GROUP BY must emit
//!                    exactly one row even when the input is empty
//!                    (SELECT count(*) FROM empty -> 0). If groups is empty
//!                    and the table is empty, FindOrCreate({}) forces it.
//!   GetData:         the operator is now a SOURCE: copy result rows into
//!                    chunk, STANDARD_VECTOR_SIZE at a time, using
//!                    emit_offset_.fetch_add to partition the rows among
//!                    threads; emit an empty chunk when done.
//!
//! Hint: the GroupByHashTable (FindOrCreate) is provided - read it first.
//! Hint: agg->child is nullptr for count(*); pass a default Value() to Update.
//!
//! Tests: Lab3ExecutionTest.GroupBy* / ParallelGroupByConsistency
//! ============================================================================

namespace {

//! ---------------------------------------------------------------------------
//! Per-group state of one aggregate function
//! ---------------------------------------------------------------------------
struct AggregateState {
	ExpressionType aggregate = ExpressionType::AGGREGATE_COUNT;
	bool has_value = false;
	int64_t count = 0;
	double sum = 0.0;
	Value value; // min/max candidate

	void Update(const Value &input) {
		// [SOLUTION BEGIN L3.T4]
		switch (aggregate) {
		case ExpressionType::AGGREGATE_COUNT_STAR:
			count++;
			return;
		case ExpressionType::AGGREGATE_COUNT:
			if (!input.IsNull()) {
				count++;
			}
			return;
		case ExpressionType::AGGREGATE_SUM:
		case ExpressionType::AGGREGATE_AVG:
			if (!input.IsNull()) {
				sum += input.GetNumeric();
				count++;
			}
			return;
		case ExpressionType::AGGREGATE_MIN:
		case ExpressionType::AGGREGATE_MAX:
			if (input.IsNull()) {
				return;
			}
			if (!has_value) {
				value = input;
				has_value = true;
				return;
			}
			if (aggregate == ExpressionType::AGGREGATE_MIN ? Value::LessThan(input, value)
			                                               : Value::LessThan(value, input)) {
				value = input;
			}
			return;
		default:
			throw ExecutorException("unknown aggregate");
		}
		// [SOLUTION END]
	}

	void Merge(const AggregateState &other) {
		// [SOLUTION BEGIN L3.T4]
		count += other.count;
		sum += other.sum;
		if (other.has_value) {
			if (!has_value) {
				value = other.value;
				has_value = true;
			} else if (aggregate == ExpressionType::AGGREGATE_MIN ? Value::LessThan(other.value, value)
			                                                      : Value::LessThan(value, other.value)) {
				value = other.value;
			}
		}
		// [SOLUTION END]
	}

	Value Finalize(const LogicalType &return_type) const {
		// [SOLUTION BEGIN L3.T4]
		switch (aggregate) {
		case ExpressionType::AGGREGATE_COUNT_STAR:
		case ExpressionType::AGGREGATE_COUNT:
			return Value::BigInt(count);
		case ExpressionType::AGGREGATE_SUM:
			if (count == 0) {
				return Value::Null(return_type);
			}
			if (return_type.Id() == LogicalTypeId::DOUBLE) {
				return Value::Double(sum);
			}
			return Value::BigInt(static_cast<int64_t>(sum));
		case ExpressionType::AGGREGATE_AVG:
			if (count == 0) {
				return Value::Null(return_type);
			}
			return Value::Double(sum / static_cast<double>(count));
		case ExpressionType::AGGREGATE_MIN:
		case ExpressionType::AGGREGATE_MAX:
			if (!has_value) {
				return Value::Null(return_type);
			}
			return value;
		default:
			throw ExecutorException("unknown aggregate");
		}
		// [SOLUTION END]
	}
};

//! ---------------------------------------------------------------------------
//! The hash table: group key -> one AggregateState per aggregate function
//! (provided - read this before writing Task L3.T4b)
//! ---------------------------------------------------------------------------
struct GroupKeyHash {
	size_t operator()(const std::vector<Value> &key) const {
		uint64_t hash = 0;
		for (const auto &value : key) {
			hash = hash * 31 + value.Hash();
		}
		return static_cast<size_t>(hash);
	}
};

struct GroupKeyEqual {
	bool operator()(const std::vector<Value> &left, const std::vector<Value> &right) const {
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
};

class GroupByHashTable {
public:
	std::vector<AggregateState> &FindOrCreate(const std::vector<Value> &key) {
		auto entry = index_.find(key);
		if (entry != index_.end()) {
			return states_[entry->second];
		}
		idx_t group_index = keys_.size();
		keys_.push_back(key);
		states_.emplace_back(aggregate_types_.size());
		for (idx_t i = 0; i < aggregate_types_.size(); i++) {
			states_[group_index][i].aggregate = aggregate_types_[i];
		}
		index_.emplace(key, group_index);
		return states_[group_index];
	}

	void SetAggregateTypes(const std::vector<ExpressionType> &types) {
		aggregate_types_ = types;
	}

	bool Empty() const {
		return keys_.empty();
	}

	std::vector<std::vector<Value>> keys_;
	std::vector<std::vector<AggregateState>> states_;

private:
	std::vector<ExpressionType> aggregate_types_;
	std::unordered_map<std::vector<Value>, idx_t, GroupKeyHash, GroupKeyEqual> index_;
};

} // namespace

class HashAggregateGlobalSinkState : public GlobalSinkState {
public:
	GroupByHashTable table;
	std::mutex lock;
};

class HashAggregateLocalSinkState : public LocalSinkState {
public:
	GroupByHashTable table;
};

PhysicalHashAggregate::PhysicalHashAggregate(
    std::vector<std::unique_ptr<BoundExpression>> groups_p,
    std::vector<std::unique_ptr<BoundAggregateExpression>> aggregates_p, std::vector<LogicalType> types_p,
    std::vector<std::string> names_p)
    : PhysicalOperator(PhysicalOperatorType::HASH_GROUP_BY, std::move(types_p)), groups(std::move(groups_p)),
      aggregates(std::move(aggregates_p)) {
	names = std::move(names_p);
}

std::unique_ptr<GlobalSinkState> PhysicalHashAggregate::GetGlobalSinkState(ExecutionContext & /*context*/) {
	auto result = std::make_unique<HashAggregateGlobalSinkState>();
	std::vector<ExpressionType> aggregate_types;
	for (const auto &agg : aggregates) {
		aggregate_types.push_back(agg->type);
	}
	result->table.SetAggregateTypes(aggregate_types);
	return result;
}

std::unique_ptr<LocalSinkState> PhysicalHashAggregate::GetLocalSinkState(ExecutionContext & /*context*/,
                                                                         GlobalSinkState & /*gstate*/) {
	auto result = std::make_unique<HashAggregateLocalSinkState>();
	std::vector<ExpressionType> aggregate_types;
	for (const auto &agg : aggregates) {
		aggregate_types.push_back(agg->type);
	}
	result->table.SetAggregateTypes(aggregate_types);
	return result;
}

void PhysicalHashAggregate::Sink(ExecutionContext & /*context*/, GlobalSinkState & /*gstate*/, LocalSinkState &lstate,
                                 DataChunk &chunk) {
	// [SOLUTION BEGIN L3.T4]
	auto &local = lstate.Cast<HashAggregateLocalSinkState>();
	std::vector<std::unique_ptr<Vector>> group_vectors;
	for (const auto &group : groups) {
		auto vector = std::make_unique<Vector>(group->return_type);
		ExpressionExecutor::Evaluate(*group, chunk, *vector);
		group_vectors.push_back(std::move(vector));
	}
	std::vector<std::unique_ptr<Vector>> arg_vectors;
	for (const auto &agg : aggregates) {
		if (!agg->child) {
			arg_vectors.push_back(nullptr);
			continue;
		}
		auto vector = std::make_unique<Vector>(agg->child->return_type);
		ExpressionExecutor::Evaluate(*agg->child, chunk, *vector);
		arg_vectors.push_back(std::move(vector));
	}
	for (idx_t row = 0; row < chunk.size(); row++) {
		std::vector<Value> key;
		for (const auto &vector : group_vectors) {
			key.push_back(vector->GetValue(row));
		}
		auto &states = local.table.FindOrCreate(key);
		for (idx_t agg_idx = 0; agg_idx < aggregates.size(); agg_idx++) {
			Value input = arg_vectors[agg_idx] ? arg_vectors[agg_idx]->GetValue(row) : Value();
			states[agg_idx].Update(input);
		}
	}
	// [SOLUTION END]
}

void PhysicalHashAggregate::Combine(ExecutionContext & /*context*/, GlobalSinkState &gstate, LocalSinkState &lstate) {
	// [SOLUTION BEGIN L3.T4]
	auto &global = gstate.Cast<HashAggregateGlobalSinkState>();
	auto &local = lstate.Cast<HashAggregateLocalSinkState>();
	std::lock_guard<std::mutex> guard(global.lock);
	for (idx_t group = 0; group < local.table.keys_.size(); group++) {
		auto &states = global.table.FindOrCreate(local.table.keys_[group]);
		for (idx_t agg_idx = 0; agg_idx < states.size(); agg_idx++) {
			states[agg_idx].Merge(local.table.states_[group][agg_idx]);
		}
	}
	// [SOLUTION END]
}

void PhysicalHashAggregate::Finalize(ExecutionContext & /*context*/, GlobalSinkState &gstate) {
	// [SOLUTION BEGIN L3.T4]
	auto &global = gstate.Cast<HashAggregateGlobalSinkState>();
	if (groups.empty() && global.table.Empty()) {
		// aggregation without GROUP BY always produces exactly one row
		global.table.FindOrCreate({});
	}
	for (idx_t group = 0; group < global.table.keys_.size(); group++) {
		std::vector<Value> row = global.table.keys_[group];
		for (idx_t agg_idx = 0; agg_idx < aggregates.size(); agg_idx++) {
			row.push_back(global.table.states_[group][agg_idx].Finalize(aggregates[agg_idx]->return_type));
		}
		result_rows_.push_back(std::move(row));
	}
	// [SOLUTION END]
}

void PhysicalHashAggregate::GetData(ExecutionContext & /*context*/, DataChunk &chunk, SourceInput & /*input*/) {
	// [SOLUTION BEGIN L3.T4]
	idx_t offset = emit_offset_.fetch_add(STANDARD_VECTOR_SIZE);
	if (offset >= result_rows_.size()) {
		chunk.SetCardinality(0);
		return;
	}
	idx_t count = std::min<idx_t>(STANDARD_VECTOR_SIZE, result_rows_.size() - offset);
	for (idx_t row = 0; row < count; row++) {
		for (idx_t col = 0; col < result_rows_[offset + row].size(); col++) {
			chunk.SetValue(col, row, result_rows_[offset + row][col]);
		}
	}
	chunk.SetCardinality(count);
	// [SOLUTION END]
}

} // namespace tiny_duckdb
