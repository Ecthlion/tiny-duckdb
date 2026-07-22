#include "tiny_duckdb/execution/operator/physical_result_collector.hpp"

#include "tiny_duckdb/main/tiny_duckdb.hpp"

namespace tiny_duckdb {

namespace {

class ResultCollectorGlobalSinkState : public GlobalSinkState {
public:
	std::vector<std::unique_ptr<DataChunk>> chunks;
	std::mutex lock;
};

class ResultCollectorLocalSinkState : public LocalSinkState {
public:
	std::vector<std::unique_ptr<DataChunk>> chunks;
};

} // namespace

PhysicalResultCollector::PhysicalResultCollector(QueryResult &result_p, std::vector<LogicalType> types_p,
                                                 std::vector<std::string> names_p)
    : PhysicalOperator(PhysicalOperatorType::RESULT_COLLECTOR, std::move(types_p)), result(result_p) {
	names = std::move(names_p);
}

std::unique_ptr<GlobalSinkState> PhysicalResultCollector::GetGlobalSinkState(ExecutionContext &/*context*/) {
	return std::make_unique<ResultCollectorGlobalSinkState>();
}

std::unique_ptr<LocalSinkState> PhysicalResultCollector::GetLocalSinkState(ExecutionContext &/*context*/,
                                                                           GlobalSinkState &/*gstate*/) {
	return std::make_unique<ResultCollectorLocalSinkState>();
}

void PhysicalResultCollector::Sink(ExecutionContext &/*context*/, GlobalSinkState &/*gstate*/, LocalSinkState &lstate,
                                   DataChunk &chunk) {
	auto &local = lstate.Cast<ResultCollectorLocalSinkState>();
	auto copy = std::make_unique<DataChunk>();
	copy->Initialize(types);
	copy->CopyFrom(chunk);
	local.chunks.push_back(std::move(copy));
}

void PhysicalResultCollector::Combine(ExecutionContext &/*context*/, GlobalSinkState &gstate, LocalSinkState &lstate) {
	auto &global = gstate.Cast<ResultCollectorGlobalSinkState>();
	auto &local = lstate.Cast<ResultCollectorLocalSinkState>();
	std::lock_guard<std::mutex> guard(global.lock);
	for (auto &chunk : local.chunks) {
		global.chunks.push_back(std::move(chunk));
	}
	local.chunks.clear();
}

void PhysicalResultCollector::Finalize(ExecutionContext &/*context*/, GlobalSinkState &gstate) {
	auto &global = gstate.Cast<ResultCollectorGlobalSinkState>();
	for (auto &chunk : global.chunks) {
		result.AddChunk(std::move(chunk));
	}
}

} // namespace tiny_duckdb
