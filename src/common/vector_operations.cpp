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
	// [SOLUTION BEGIN L5.T1]
	ValidateDimensions(left, right);
	double squared_distance = 0;
	for (size_t i = 0; i < left.size(); i++) {
		const double delta = left[i] - right[i];
		squared_distance += delta * delta;
	}
	return std::sqrt(squared_distance);
	// [SOLUTION END]
}

double VectorOperations::CosineDistance(const std::vector<double> &left, const std::vector<double> &right) {
	// [SOLUTION BEGIN L5.T1]
	ValidateDimensions(left, right);
	double dot = 0;
	double left_norm_squared = 0;
	double right_norm_squared = 0;
	for (size_t i = 0; i < left.size(); i++) {
		dot += left[i] * right[i];
		left_norm_squared += left[i] * left[i];
		right_norm_squared += right[i] * right[i];
	}
	if (left_norm_squared == 0 || right_norm_squared == 0) {
		throw ExecutorException("cosine distance is undefined for a zero vector");
	}
	return 1.0 - dot / (std::sqrt(left_norm_squared) * std::sqrt(right_norm_squared));
	// [SOLUTION END]
}

double VectorOperations::NegativeInnerProduct(const std::vector<double> &left,
                                              const std::vector<double> &right) {
	// [SOLUTION BEGIN L5.T1]
	ValidateDimensions(left, right);
	double dot = 0;
	for (size_t i = 0; i < left.size(); i++) {
		dot += left[i] * right[i];
	}
	return -dot;
	// [SOLUTION END]
}

} // namespace tiny_duckdb
