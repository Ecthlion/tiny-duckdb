#pragma once

#include <atomic>

#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/common/types.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 0 - C++ Primer: the MorselQueue
//!
//! A MorselQueue hands out morsel ids [0, total) to worker threads. It is the
//! scheduling primitive at the heart of morsel-driven parallelism (Lab 3):
//! every worker thread calls NextMorsel() in a loop, and each morsel must be
//! handed to EXACTLY ONE thread - no duplicates, no losses, no locks.
//!
//! ----------------------------------------------------------------------------
//! Task L0.T1 - MorselQueue::NextMorsel
//!
//! Make NextMorsel thread-safe. Contract:
//!   * returns true and writes the next id into morsel_id while ids remain;
//!   * returns false once all `total` ids have been handed out - and keeps
//!     returning false forever after (no wraparound);
//!   * over the whole run, every id in [0, total) is handed out exactly once,
//!     no matter how many threads call concurrently.
//!
//! Hint: a single std::atomic<idx_t>::fetch_add is the whole solution. Think
//!       about the difference between the returned value and the incremented
//!       value, and why a mutex would be overkill (and slow) here.
//!
//! Tests: Lab0MorselTest.* (sequential dispatch, exhaustion, and a
//!        4-thread exactly-once stress test over 10000 morsels)
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
