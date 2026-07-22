# Lab 0 - C++ 热身：Morsel 队列

> 对应代码：`src/include/tiny_duckdb/execution/morsel_queue.hpp`
> 测试：`test/lab0_morsel_test.cpp`

## 背景：为什么数据库需要 morsel？

OLAP 查询动辄扫描上亿行数据，必须用上所有 CPU 核。最朴素的并行方式是**静态划分**：把数据平均切成 N 份，N 个线程各扫一份。问题是各份的工作量很难完全相等（有的行组可能因为 zone map 被整个跳过），总有一个线程最后还在干活，其他线程已经闲置——拖尾（straggler）问题。

DuckDB/Hyper/Umbra 一脉的解法是 **morsel-driven 并行**（Leis et al., SIGMOD'14）：

- 数据被切成许多**小而多的工作单元（morsel）**，通常就是一个向量化大小的切片；
- 所有 worker 线程围绕一个**共享队列**，干完一个 morsel 就来领下一个；
- 快的线程自然多领，慢的线程少领——**动态负载均衡**，拖尾被摊平到 morsel 粒度。

Lab 3 的表扫描（`ParallelTableScanState`）就是按这个思想实现的。本 Lab 先实现它背后的调度原语。

## 任务 L0.T1：`MorselQueue::NextMorsel`

```cpp
class MorselQueue {
    bool NextMorsel(idx_t &morsel_id);   // 领下一个 morsel；领完返回 false
    idx_t total_;                        // morsel 总数
    std::atomic<idx_t> next_;            // 下一个待分发的 id
};
```

要求：

1. 每个 morsel id `[0, total)` 恰好被分发一次（exactly-once）；
2. 线程安全，且**不允许使用锁**；
3. 提示：一次 `fetch_add` 就够了。想想为什么 `fetch_add` 即使"越过终点"也是安全的。

### 思考题

- 为什么用 `std::memory_order_relaxed` 就够了？换成 `seq_cst` 会改变正确性吗，只改变什么？
- 如果把"判满 + 返回 id"拆成两步（先 `load` 再 `compare_exchange`），会引入什么问题？

## 测试

```bash
make tdbtest && ./tdbtest
# [  OK  ] Lab0MorselTest.SequentialDispatch
# [  OK  ] Lab0MorselTest.EmptyQueue
# [  OK  ] Lab0MorselTest.ConcurrentExactlyOnce   <- 4 线程抢 10000 个 morsel
```

`ConcurrentExactlyOnce` 会收集 4 个线程领到的所有 morsel id，排序后必须恰好是 `0..9999`——少一个、多一个、重复一个都会失败。
