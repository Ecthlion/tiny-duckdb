#pragma once

#include "tiny_duckdb/binder/bound_expression.hpp"
#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/common/vector.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 3 (Task L3.T1) - the ExpressionExecutor: vectorized expression
//! evaluation over a DataChunk.
//!
//! Evaluate() computes one output Vector per expression; Select() evaluates a
//! boolean expression and returns the matching row indexes as a selection
//! vector - the filter's zero-copy trick.
//! ============================================================================
class ExpressionExecutor {
public:
	//! Evaluate the expression over the input chunk into result
	static void Evaluate(const BoundExpression &expr, DataChunk &chunk, Vector &result);

	//! Evaluate a boolean expression; write matching row indexes into sel and
	//! return how many rows matched
	static idx_t Select(const BoundExpression &expr, DataChunk &chunk, SelectionVector &sel);
};

} // namespace tiny_duckdb
