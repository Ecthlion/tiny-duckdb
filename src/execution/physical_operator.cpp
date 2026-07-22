#include "tiny_duckdb/execution/physical_operator.hpp"

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

//! The base implementations throw: operators override exactly the interfaces
//! they implement (source/operator/sink), so a wrong pipeline shape fails loudly.

std::unique_ptr<OperatorState> PhysicalOperator::GetOperatorState(ExecutionContext &/*context*/) {
	return nullptr;
}

std::unique_ptr<GlobalSourceState> PhysicalOperator::GetGlobalSourceState(ExecutionContext &/*context*/) {
	return nullptr;
}

void PhysicalOperator::GetData(ExecutionContext &/*context*/, DataChunk &/*chunk*/, SourceInput &/*input*/) {
	throw ExecutorException("GetData not implemented for this operator");
}

void PhysicalOperator::Execute(ExecutionContext &/*context*/, DataChunk &/*chunk*/, OperatorState &/*state*/) {
	throw ExecutorException("Execute not implemented for this operator");
}

std::unique_ptr<GlobalSinkState> PhysicalOperator::GetGlobalSinkState(ExecutionContext &/*context*/) {
	return nullptr;
}

std::unique_ptr<LocalSinkState> PhysicalOperator::GetLocalSinkState(ExecutionContext &/*context*/,
                                                                    GlobalSinkState &/*gstate*/) {
	return nullptr;
}

void PhysicalOperator::Sink(ExecutionContext &/*context*/, GlobalSinkState &/*gstate*/, LocalSinkState &/*lstate*/,
                            DataChunk &/*chunk*/) {
	throw ExecutorException("Sink not implemented for this operator");
}

void PhysicalOperator::Combine(ExecutionContext &/*context*/, GlobalSinkState &/*gstate*/, LocalSinkState &/*lstate*/) {
	throw ExecutorException("Combine not implemented for this operator");
}

void PhysicalOperator::Finalize(ExecutionContext &/*context*/, GlobalSinkState &/*gstate*/) {
	throw ExecutorException("Finalize not implemented for this operator");
}

} // namespace tiny_duckdb
