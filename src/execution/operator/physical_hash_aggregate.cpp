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
		// TODO(L3.T4): implement this (see the corresponding docs/labN.md)
		throw NotImplementedException("task L3.T4 not implemented yet");
	}

	void Merge(const AggregateState &other) {
		// TODO(L3.T4): implement this (see the corresponding docs/labN.md)
		throw NotImplementedException("task L3.T4 not implemented yet");
	}

	Value Finalize(const LogicalType &return_type) const {
		// TODO(L3.T4): implement this (see the corresponding docs/labN.md)
		throw NotImplementedException("task L3.T4 not implemented yet");
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
	// TODO(L3.T4): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T4 not implemented yet");
}

void PhysicalHashAggregate::Combine(ExecutionContext & /*context*/, GlobalSinkState &gstate, LocalSinkState &lstate) {
	// TODO(L3.T4): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T4 not implemented yet");
}

void PhysicalHashAggregate::Finalize(ExecutionContext & /*context*/, GlobalSinkState &gstate) {
	// TODO(L3.T4): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T4 not implemented yet");
}

void PhysicalHashAggregate::GetData(ExecutionContext & /*context*/, DataChunk &chunk, SourceInput & /*input*/) {
	// TODO(L3.T4): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L3.T4 not implemented yet");
}

} // namespace tiny_duckdb
