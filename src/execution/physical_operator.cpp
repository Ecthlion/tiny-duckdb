#include "tiny_duckdb/execution/physical_operator.hpp"

#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/execution/execution_context.hpp"

namespace tiny_duckdb {

std::unique_ptr<OperatorState> PhysicalOperator::GetOperatorState(ExecutionContext & /*context*/) {
	return std::make_unique<OperatorState>();
}

std::unique_ptr<GlobalSourceState> PhysicalOperator::GetGlobalSourceState(ExecutionContext & /*context*/) {
	return nullptr;
}

void PhysicalOperator::GetData(ExecutionContext & /*context*/, DataChunk & /*chunk*/, SourceInput & /*input*/) {
	throw ExecutorException("GetData called on an operator that is not a source");
}

OperatorResultType PhysicalOperator::Execute(ExecutionContext & /*context*/, DataChunk & /*chunk*/,
                                             OperatorState & /*state*/) {
	throw ExecutorException("Execute called on an operator that is not a pipeline operator");
}

std::unique_ptr<GlobalSinkState> PhysicalOperator::GetGlobalSinkState(ExecutionContext & /*context*/) {
	return nullptr;
}

std::unique_ptr<LocalSinkState> PhysicalOperator::GetLocalSinkState(ExecutionContext & /*context*/,
                                                                    GlobalSinkState & /*gstate*/) {
	return nullptr;
}

void PhysicalOperator::Sink(ExecutionContext & /*context*/, GlobalSinkState & /*gstate*/, LocalSinkState & /*lstate*/,
                            DataChunk & /*chunk*/) {
	throw ExecutorException("Sink called on an operator that is not a sink");
}

void PhysicalOperator::Combine(ExecutionContext & /*context*/, GlobalSinkState & /*gstate*/, LocalSinkState & /*lstate*/) {
}

void PhysicalOperator::Finalize(ExecutionContext & /*context*/, GlobalSinkState & /*gstate*/) {
}

} // namespace tiny_duckdb
