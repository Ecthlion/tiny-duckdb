#pragma once

#include <atomic>

#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/common/types.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 0 - C++ Primer
//!
//! A MorselQueue hands out morsel ids [0, total) to worker threads. It is the
//! scheduling primitive at the heart of morsel-driven parallelism (Lab 3):
//! every worker thread calls NextMorsel() in a loop, and each morsel is handed
//! to exactly one thread.
//!
//! Task L0.T1: implement NextMorsel so that it is thread-safe. Hint: one
//! atomic fetch_add is all you need - no locks required.
//! ============================================================================
class MorselQueue {
public:
	explicit MorselQueue(idx_t total_morsels) : total_(total_morsels), next_(0) {
	}

	//! Grab the next morsel. Returns false when all morsels are taken.
	bool NextMorsel(idx_t &morsel_id) {
		// [SOLUTION BEGIN L0.T1]
		const idx_t morsel = next_.fetch_add(1, std::memory_order_relaxed);
		if (morsel >= total_) {
			return false;
		}
		morsel_id = morsel;
		return true;
		// [SOLUTION END]
	}

	idx_t TotalMorsels() const {
		return total_;
	}

private:
	idx_t total_;
	std::atomic<idx_t> next_;
};

} // namespace tiny_duckdb
