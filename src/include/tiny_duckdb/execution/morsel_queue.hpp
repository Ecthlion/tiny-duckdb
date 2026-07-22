#pragma once

#include <atomic>

#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/common/types.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 0 - C++ warmup: the MorselQueue
//!
//! Morsel-driven parallelism (Leis et al., SIGMOD'14) splits the input into
//! many small units of work ("morsels") and lets every worker thread grab
//! the next morsel whenever it finishes the previous one. Fast threads do
//! more work, slow threads do less - no stragglers, no static partitioning.
//!
//! Task L0.T1: implement NextMorsel() so every morsel id in [0, total) is
//!             handed out exactly once, from multiple threads, without locks.
//!             Hint: std::atomic::fetch_add.
//! ============================================================================
class MorselQueue {
public:
	explicit MorselQueue(idx_t total) : total_(total), next_(0) {
	}

	//! Grab the next morsel id. Returns false when the queue is exhausted.
	bool NextMorsel(idx_t &morsel_id) {
		// [SOLUTION BEGIN L0.T1]
		const idx_t id = next_.fetch_add(1, std::memory_order_relaxed);
		if (id >= total_) {
			return false;
		}
		morsel_id = id;
		return true;
		// [SOLUTION END]
	}

private:
	idx_t total_;
	std::atomic<idx_t> next_;
};

} // namespace tiny_duckdb
