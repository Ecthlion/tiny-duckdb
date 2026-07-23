#pragma once

#include <vector>

namespace tiny_duckdb {

//! Exact distance kernels used by Lab 5. SQL binding and NULL propagation
//! deliberately live in higher layers.
class VectorOperations {
public:
	static double L2Distance(const std::vector<double> &left, const std::vector<double> &right);
	static double CosineDistance(const std::vector<double> &left, const std::vector<double> &right);
	static double NegativeInnerProduct(const std::vector<double> &left, const std::vector<double> &right);

private:
	static void ValidateDimensions(const std::vector<double> &left, const std::vector<double> &right);
};

} // namespace tiny_duckdb
