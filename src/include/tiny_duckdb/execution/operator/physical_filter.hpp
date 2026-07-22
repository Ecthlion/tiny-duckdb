#pragma once

#include <memory>
#include <vector>

#include "tiny_duckdb/binder/bound_expression.hpp"
#include "tiny_duckdb/execution/physical_operator.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 3 (Task L3.T2) - filter and projection: the mid-pipeline OPERATORs
//! ============================================================================

//! OPERATOR. Execute() evaluates the predicate and compacts the chunk in
//! place using DataChunk::Slice.
class PhysicalFilter : public PhysicalOperator {
public:
	PhysicalFilter(std::unique_ptr<BoundExpression> predicate_p, std::vector<LogicalType> types,
	               std::vector<std::string> names);

	std::unique_ptr<OperatorState> GetOperatorState(ExecutionContext &context) override;
	OperatorResultType Execute(ExecutionContext &context, DataChunk &chunk, OperatorState &state) override;

	std::unique_ptr<BoundExpression> predicate;
};

//! OPERATOR. Execute() evaluates one expression per output column and
//! replaces the chunk with the result.
class PhysicalProjection : public PhysicalOperator {
public:
	PhysicalProjection(std::vector<std::unique_ptr<BoundExpression>> expressions_p,
	                   std::vector<LogicalType> types, std::vector<std::string> names);

	std::unique_ptr<OperatorState> GetOperatorState(ExecutionContext &context) override;
	OperatorResultType Execute(ExecutionContext &context, DataChunk &chunk, OperatorState &state) override;

	std::vector<std::unique_ptr<BoundExpression>> expressions;
};

} // namespace tiny_duckdb
