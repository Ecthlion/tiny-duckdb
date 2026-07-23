#include "tiny_duckdb/common/vector_operations.hpp"

#include <cmath>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 5 - TASK #1: exact vector distance kernels
//!
//! Implement the formulas over two equal-dimensional arrays. The SQL layer
//! has already checked dimensions, but the kernels keep a defensive check so
//! they are safe to call directly from tests or future operators.
//! ============================================================================

void VectorOperations::ValidateDimensions(const std::vector<double> &left, const std::vector<double> &right) {
	if (left.empty() || left.size() != right.size()) {
		throw ExecutorException("vector distance requires two non-empty vectors with the same dimension");
	}
}

double VectorOperations::L2Distance(const std::vector<double> &left, const std::vector<double> &right) {
	// TODO(L5.T1): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L5.T1 not implemented yet");
}

double VectorOperations::CosineDistance(const std::vector<double> &left, const std::vector<double> &right) {
	// TODO(L5.T1): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L5.T1 not implemented yet");
}

double VectorOperations::NegativeInnerProduct(const std::vector<double> &left,
                                              const std::vector<double> &right) {
	// TODO(L5.T1): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L5.T1 not implemented yet");
}

} // namespace tiny_duckdb
