#include "tiny_duckdb/execution/operator/physical_hash_aggregate.hpp"

#include <unordered_map>

#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/execution/expression_executor.hpp"

namespace tiny_duckdb {

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
	}

	void Merge(const AggregateState &other) {
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
	}

	Value Finalize(const LogicalType &return_type) const {
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
	}
};

//! ---------------------------------------------------------------------------
//! The hash table: group key -> one AggregateState per aggregate function
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

std::unique_ptr<GlobalSinkState> PhysicalHashAggregate::GetGlobalSinkState(ExecutionContext &/*context*/) {
	auto result = std::make_unique<HashAggregateGlobalSinkState>();
	std::vector<ExpressionType> aggregate_types;
	for (const auto &agg : aggregates) {
		aggregate_types.push_back(agg->type);
	}
	result->table.SetAggregateTypes(aggregate_types);
	return result;
}

std::unique_ptr<LocalSinkState> PhysicalHashAggregate::GetLocalSinkState(ExecutionContext &/*context*/,
                                                                         GlobalSinkState &/*gstate*/) {
	auto result = std::make_unique<HashAggregateLocalSinkState>();
	std::vector<ExpressionType> aggregate_types;
	for (const auto &agg : aggregates) {
		aggregate_types.push_back(agg->type);
	}
	result->table.SetAggregateTypes(aggregate_types);
	return result;
}

void PhysicalHashAggregate::Sink(ExecutionContext &/*context*/, GlobalSinkState &/*gstate*/, LocalSinkState &lstate,
                                 DataChunk &chunk) {
	auto &local = lstate.Cast<HashAggregateLocalSinkState>();
	// ------------------------------------------------------------------
	// L3.T4: evaluate group keys and aggregate arguments vector-wise,
	// then fold every row into the thread-local hash table (no locking).
	// ------------------------------------------------------------------
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
}

void PhysicalHashAggregate::Combine(ExecutionContext &/*context*/, GlobalSinkState &gstate, LocalSinkState &lstate) {
	auto &global = gstate.Cast<HashAggregateGlobalSinkState>();
	auto &local = lstate.Cast<HashAggregateLocalSinkState>();
	std::lock_guard<std::mutex> guard(global.lock);
	for (idx_t group = 0; group < local.table.keys_.size(); group++) {
		auto &states = global.table.FindOrCreate(local.table.keys_[group]);
		for (idx_t agg_idx = 0; agg_idx < states.size(); agg_idx++) {
			states[agg_idx].Merge(local.table.states_[group][agg_idx]);
		}
	}
}

void PhysicalHashAggregate::Finalize(ExecutionContext &/*context*/, GlobalSinkState &gstate) {
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
}

void PhysicalHashAggregate::GetData(ExecutionContext &/*context*/, DataChunk &chunk, SourceInput &/*input*/) {
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
}

} // namespace tiny_duckdb
