# Lab 0 - C++ Primer：MorselQueue

> 对应代码：`src/include/tiny_duckdb/execution/morsel_queue.hpp`
> 对应测试：`test/lab0_morsel_test.cpp`（5 个用例）

## Overview

本课程的所有实验都在 tiny-duckdb 上进行——一个为教学从零实现的 OLAP 数据库，架构上浓缩自 DuckDB。在动手存储与执行引擎之前，我们先用一个小任务确认你的 C++（尤其是并发）基础，并顺便埋下整门课最重要的一颗种子：**morsel**。

本 Lab 只有一个任务：

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| Task L0.T1 | `MorselQueue::NextMorsel`（无锁线程安全发牌器） | `./tdbtest Lab0` 5 个用例全过 |

这个 5 行实现的原子类，就是 Lab 3 里多线程扫描引擎的调度核心。请认真对待它——Lab 3 的 `ParallelTableScanState` 就是它的直接后裔。

这是一个**个人**实验：你可以和同学讨论思路，但代码必须自己写。

## Background

### 什么是 morsel？

分析型查询要扫上亿行数据，单线程扫太慢。最直观的并行是把数据**静态**切成 N 份、N 个线程各扫一份——但只要有一份落在慢核上（操作系统调度、缓存抖动），整体就被拖尾。morsel-driven parallelism（Leis et al., SIGMOD'14，DuckDB/Umbra/Hyper 的调度模型）的答案是把输入切成**远多于线程数**的小块（morsel，比如每块 10 万行），用一个共享的**发牌器**动态分发：

```
            ┌──────────────┐
 thread 1 ──┤              │
 thread 2 ──┤  MorselQueue  │── morsel 0, 1, 2, ... 各发一次
 thread 3 ──┤  (atomic)    │
 thread 4 ──┤              │
            └──────────────┘
```

干得快的工作线程自然多领 morsel，慢的自然少领——**负载均衡是免费的**，不需要任何调度器。代价只是每领一次 morsel 一次原子操作，相对于处理 10 万行可以忽略。

在 tiny-duckdb 里，一个 morsel 就是"一个行组（Lab 1）里的 STANDARD_VECTOR_SIZE 行"。Lab 3 你会亲手把它接进流水线。

### C++ 原子操作速成

`std::atomic<idx_t>` 保证对它的每次读-改-写不可被分割。关键接口：

| 操作 | 语义 | 返回值 |
|------|------|--------|
| `load()` | 读当前值 | 当前值 |
| `store(x)` | 写入 x | void |
| `fetch_add(n)` | **原子地**加 n | **加之前的旧值** |

经典错误：`idx_t old = a.load(); a.store(old + 1);`——两个线程可以同时读到同一个 `old`，各自 +1 写回，结果只加了一次（lost update）。`fetch_add` 把这两步合成一条硬件指令（x86 的 `lock xadd`），从根本上消除这个窗口。

## Task #1 - MorselQueue::NextMorsel

实现：

```cpp
class MorselQueue {
public:
    explicit MorselQueue(idx_t total_morsels);
    bool NextMorsel(idx_t &morsel_id);   // <- 你要实现的
private:
    idx_t total_;
    std::atomic<idx_t> next_;
};
```

**契约**（测试逐条检查这些）：

1. 还有 morsel 未发完时返回 `true`，并把下一个 id 写入 `morsel_id`（id 从 0 开始）；
2. `total` 个 id 全部发出后返回 `false`——且**永远**保持返回 `false`（不能回绕）；
3. 全程每个 id ∈ [0, total) 恰好被发出**一次**：多线程并发调用也不能重复、不能遗漏；
4. 不允许使用互斥锁（本任务的用意就是无锁编程）。

**Hint**：一次 `next_.fetch_add(1)` 就是全部答案。想清楚两件事：（a）`fetch_add` 返回的是加之前的旧值；（b）当旧值已经 ≥ `total_` 时意味着什么——多个线程可能同时越过终点，这不构成错误。

**Hint**：先让单线程测试通过，再想并发。并发正确性由 `ConcurrentExactlyOnce`（4 线程瓜分 10000 个 morsel，汇总排序后必须恰好是 0..9999）和 `SingleProducerManyThreads`（8 线程抢 1 个 morsel，恰好 1 个成功）检验。

## Testing

```bash
make -j$(nproc)        # 编译
./tdbtest Lab0         # 只跑 Lab 0 的 5 个用例（套件名过滤）
./tdbtest              # 跑全部 Lab（此时只有 Lab 0 应该过，其余 Lab 的桩会失败——正常）
```

预期输出：

```
[ RUN  ] Lab0MorselTest.SequentialDispatch
[  OK  ] Lab0MorselTest.SequentialDispatch
...
5 passed, 0 failed
```

## Development Hints

- 怀疑并发 bug 时，把 `ConcurrentExactlyOnce` 里的 TOTAL 调大、线程数调高，连跑几十次——竞态几乎必然现形。
- `std::memory_order_relaxed` 对本任务足够：我们只要求"每个 id 发一次"，不依赖原子变量去同步其他数据。对内存序感兴趣，推荐阅读 *C++ Concurrency in Action* 第 5 章。
- 不要为通过性能测试而引入全局锁——本 Lab 的验收点恰恰是**没有**锁。

## Grading Rubric

1. `./tdbtest Lab0` 全部通过（包括并发压力测试）；
2. 不使用互斥锁（`std::mutex` 出现在你的实现里即不合格）；
3. 不出现编译警告（`make` 输出必须干净，我们开了 `-Wall -Wextra`）。

## 思考题（不计分）

- 如果把 `fetch_add` 换成 `compare_exchange_weak` 循环，语义相同，性能差多少？为什么现代 CPU 上前者是单指令？
- morsel 取多大合适？太小会怎样，太大会怎样？Lab 3 结尾你会用真实查询回答这个问题。
