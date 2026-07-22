#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/execution/physical_operator.hpp"

namespace tiny_duckdb {

class QueryResult;

//! The final sink of every SELECT pipeline tree: collects chunks into the
//! QueryResult handed back to the user.
class PhysicalResultCollector : public PhysicalOperator {
public:
	PhysicalResultCollector(QueryResult &result, std::vector<LogicalType> types, std::vector<std::string> names);

	std::unique_ptr<GlobalSinkState> GetGlobalSinkState(ExecutionContext &context) override;
	std::unique_ptr<LocalSinkState> GetLocalSinkState(ExecutionContext &context, GlobalSinkState &gstate) override;
	void Sink(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate, DataChunk &chunk) override;
	void Combine(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate) override;
	void Finalize(ExecutionContext &context, GlobalSinkState &gstate) override;

	QueryResult &result;
};

} // namespace tiny_duckdb
