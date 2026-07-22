#include <algorithm>
#include <thread>
#include <vector>

#include "tdbtest.h"
#include "tiny_duckdb/execution/morsel_queue.hpp"

using namespace tiny_duckdb;

// ---------------------------------------------------------------------------
// LAB 0 - the MorselQueue
// ---------------------------------------------------------------------------

TEST(Lab0MorselTest, SequentialDispatch) {
	MorselQueue queue(3);
	idx_t morsel = 42;
	EXPECT_TRUE(queue.NextMorsel(morsel));
	EXPECT_EQ(morsel, 0);
	EXPECT_TRUE(queue.NextMorsel(morsel));
	EXPECT_EQ(morsel, 1);
	EXPECT_TRUE(queue.NextMorsel(morsel));
	EXPECT_EQ(morsel, 2);
	EXPECT_FALSE(queue.NextMorsel(morsel));
}

TEST(Lab0MorselTest, EmptyQueue) {
	MorselQueue queue(0);
	idx_t morsel;
	EXPECT_FALSE(queue.NextMorsel(morsel));
}

TEST(Lab0MorselTest, ConcurrentExactlyOnce) {
	constexpr idx_t TOTAL = 10000;
	MorselQueue queue(TOTAL);
	std::vector<std::vector<idx_t>> per_thread(4);
	std::vector<std::thread> threads;
	for (idx_t t = 0; t < 4; t++) {
		threads.emplace_back([&queue, &per_thread, t] {
			idx_t morsel;
			while (queue.NextMorsel(morsel)) {
				per_thread[t].push_back(morsel);
			}
		});
	}
	for (auto &thread : threads) {
		thread.join();
	}
	// every morsel handed out exactly once
	std::vector<idx_t> all;
	for (const auto &list : per_thread) {
		all.insert(all.end(), list.begin(), list.end());
	}
	std::sort(all.begin(), all.end());
	EXPECT_EQ(all.size(), TOTAL);
	for (idx_t i = 0; i < TOTAL; i++) {
		EXPECT_EQ(all[i], i);
	}
}
