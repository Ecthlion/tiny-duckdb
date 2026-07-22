#pragma once

#include "tiny_duckdb/binder/bound_expression.hpp"
#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/common/vector.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 3 (Task L3.T1) - the ExpressionExecutor
//!
//! Evaluates a bound expression against an input DataChunk, producing one
//! output Vector. Also evaluates filter predicates into a SelectionVector.
//!
//! Note: NULL semantics are simplified - a comparison involving NULL produces
//! NULL, and conjunctions treat NULL as false (documented in docs/lab3.md).
//! ============================================================================
class ExpressionExecutor {
public:
	//! Evaluate expr over chunk into result (result has expr.return_type)
	static void Evaluate(const BoundExpression &expr, DataChunk &chunk, Vector &result);

	//! Evaluate a boolean expression; write matching row indexes into sel.
	//! Returns the number of matching rows.
	static idx_t Select(const BoundExpression &expr, DataChunk &chunk, SelectionVector &sel);
};

} // namespace tiny_duckdb
