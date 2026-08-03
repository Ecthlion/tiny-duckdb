# Lab 0 - C++ Primer：MorselQueue，并行调度的第一块砖

> 对应代码：[morsel_queue.hpp](file:///Users/bytedance/Projects/tiny-duckdb/src/include/tiny_duckdb/execution/morsel_queue.hpp)
> 对应测试：[lab0_morsel_test.cpp](file:///Users/bytedance/Projects/tiny-duckdb/test/lab0_morsel_test.cpp)（5 个用例）
> 卡住时按层查看：[Lab 0 渐进式 Hints](hints/lab0-hints.md)

## Overview

本课程用 5 个 Lab 从零搭一台 OLAP 数据库，架构浓缩自 DuckDB。Lab 0 是开胃菜——**只有一个任务，7 行代码**——但这 7 行是整台引擎并行调度的基石。你在 Lab 3 写并行扫描、在 Lab 4 看 DuckDB 的 Parquet 并行读取，本质都是这同一个原子发牌器。

```
┌─────────────────────────────────────────────────────────────────────┐
│                        tiny-duckdb 架构全景                         │
│                                                                     │
│  Lab 2: SQL 前端       Lab 3: 执行引擎          Lab 1: 存储层       │
│  ┌──────────┐         ┌───────────────┐        ┌──────────────┐     │
│  │ Parser   │         │ Pipeline      │        │ TableData    │     │
│  │ Binder   │ ──────► │ ┌───────────┐ │ ─────► │ ┌──────────┐ │     │
│  │ Planner  │  计划    │ │ Executor  │ │ morsel │ │ RowGroup │ │     │
│  └──────────┘         │ └───────────┘ │ 驱动   │ │ ┌──────┐ │ │     │
│                       │ MorselQueue ★│ │ 并行   │ │ │Column│ │ │     │
│                       │ (本 Lab)     │ │ 扫描   │ │ │Chunk │ │ │     │
│                       └───────────────┘        │ │ └──────┘ │ │     │
│                                                │ └──────────┘ │     │
│  Lab 4: LakeBase    Lab 5: 向量检索             └──────────────┘     │
│  ┌────────────┐    ┌──────────────┐                                 │
│  │ 湖表/Parquet│    │ 距离函数/TopK│                                 │
│  └────────────┘    └──────────────┘                                 │
└─────────────────────────────────────────────────────────────────────┘
```

你要实现的 MorselQueue 就在图中 ★ 标记的位置：执行引擎的最核心调度原语。

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| Task L0.T1 | `MorselQueue::NextMorsel`（无锁线程安全发牌器） | `./tdbtest Lab0` 5 个用例全过 |

这是一个**个人**实验：你可以和同学讨论思路，但代码必须自己写。

## Background

### 什么是 morsel？为什么需要它？

分析型查询要扫上亿行数据。最朴素的并行方案——**静态切分**：把数据切成 N 份，N 个线程各扫一份——有一个致命问题：**拖尾效应（straggler）**。

```
静态切分（不好）：
  线程 0: [0   ~ 2500)  ████████████  很快扫完 → 闲着
  线程 1: [2500~ 5000)  ████████      较快     → 闲着
  线程 2: [5000~ 7500)  ████████████  很快     → 闲着
  线程 3: [7500~10000)  ████████████░ 遇到缓存抖动/OS调度 → 整体等它！
```

只要有一份落在慢核上（操作系统抢占、缓存失效、数据本身分布不均），其他线程早干完了也只能等——**木桶的最短板决定总时延**。

**morsel-driven parallelism**（Leis et al., SIGMOD'14，DuckDB/Umbra/Hyper 的核心调度模型）的答案很简单：把输入切成**远多于线程数**的小块（morsel），用一个共享的**原子发牌器**动态分发：

```
MorselQueue（原子发牌器）：[0] [1] [2] [3] [4] [5] ... [M-1]
                                    ↑
                              next_ (atomic idx_t)

  线程 0: 抢 morsel 0 → 处理完 → 抢 morsel 4 → 处理完 → 抢 morsel 8 → ...
  线程 1: 抢 morsel 1 → 处理完 → 抢 morsel 5 → 处理完 → 抢 morsel 9 → ...
  线程 2: 抢 morsel 2 → 遇到慢数据块 → 慢慢处理 ...
  线程 3: 抢 morsel 3 → 处理完 → 抢 morsel 6 → 抢 7 → 抢 10 → ...
```

干得快的线程自然多领，慢的自然少领——**负载均衡是免费的**，不需要任何集中调度器。代价只是每领一个 morsel 一次原子操作（纳秒级），相对于处理 2048 行数据（微秒级）可以忽略。

### 在 tiny-duckdb 中，一个 morsel 到底是什么？

在 tiny-duckdb 里，morsel 和存储/执行的两个核心常量直接相关（定义在 [types.hpp](file:///Users/bytedance/Projects/tiny-duckdb/src/include/tiny_duckdb/common/types.hpp#L15-L17)）：

```cpp
static constexpr idx_t STANDARD_VECTOR_SIZE = 2048;  // 一个 Vector/DataChunk 的行数
static constexpr idx_t ROW_GROUP_SIZE      = 4 * STANDARD_VECTOR_SIZE;  // = 8192 行
```

morsel 的具体含义由 source 算子定义。对 TableScan（Lab 3 实现），一个 morsel = "某个 RowGroup 里的一段 ≤2048 行"：

```
TableData（一张表）
  └ RowGroup 0 (最多 8192 行)
      └ morsel 0: 行 [0,    2048)
      └ morsel 1: 行 [2048, 4096)
      └ morsel 2: 行 [4096, 6144)
      └ morsel 3: 行 [6144, 8192)
  └ RowGroup 1
      └ morsel 4: 行 [0,    2048)
      └ morsel 5: ...
  └ ...
```

这些 morsel 在 `ParallelTableScanState::Initialize()`（Lab 1/3 提供的代码，[table_data.cpp#L9-L23](file:///Users/bytedance/Projects/tiny-duckdb/src/storage/table_data.cpp#L9-L23)）里被切成一个数组，然后用**和你 Lab0 写的一模一样**的 `fetch_add` 逻辑来派发——你写的 `MorselQueue` 是通用发牌器，而 `ParallelTableScanState` 是它的直接特化版本。

看一下你写完 Lab0 之后、Lab3 里的实际调用方式（[physical_table_scan.cpp](file:///Users/bytedance/Projects/tiny-duckdb/src/execution/operator/physical_table_scan.cpp)）：

```cpp
void PhysicalTableScan::GetData(ExecutionContext&, DataChunk& chunk, SourceInput& input) {
    auto& gstate = input.global_state->Cast<TableScanGlobalSourceState>();
    TableScanMorsel morsel;
    while (gstate.scan_state->NextMorsel(morsel)) {   // ← 就是你写的发牌器！
        bool pruned = false;
        for (const auto& filter : table_filters) {
            // zone map 在 RowGroup 粒度判断：整组不可能匹配则跳过
            if (!table.CheckZoneMap(morsel.row_group_index,
                                    filter.column_id, filter.constant,
                                    filter.comparison)) {
                pruned = true;
                break;
            }
        }
        if (pruned) continue;                         // 整组跳过，回来抢下一个
        table.Scan(morsel, column_ids, chunk);        // 扫描 ≤2048 行
        return;
    }
    chunk.SetCardinality(0);  // 发完了 → 空 chunk = 流结束
}
```

每个工作线程都在这个 while 循环里：抢 morsel → 扫数据 → 推给下游算子 → 回来抢下一个。**你在 Lab0 写的 7 行代码驱动着整个查询的所有并行工作线程。**

### C++ 原子操作速成

`std::atomic<idx_t>` 保证对它的每次读-改-写是不可分割的。本任务只需要一个接口：

| 操作 | 语义 | 返回值 |
|------|------|--------|
| `fetch_add(n)` | **原子地**加 n | **加之前的旧值** |
| `load()` | 读当前值 | 当前值 |
| `store(x)` | 写入 x | void |

**经典错误**（lost update）：
```cpp
idx_t old = next_.load();   // 线程 A 和 B 可能同时读到同一个 old
next_.store(old + 1);       // 两个线程都写 old+1，结果只加了一次！
```

`fetch_add` 把"读旧值 → 加 1 → 写回"三步合成一条硬件指令（x86 上是 `lock xadd`），从根本上消除了这个竞态窗口。

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

**契约**（测试逐条验证这些性质）：

1. 还有 morsel 未发完时返回 `true`，并把下一个 id 写入 `morsel_id`（id 从 0 开始，连续递增）；
2. `total_` 个 id 全部发出后返回 `false`——且**永远**保持返回 `false`（不能回绕/重置）；
3. 全程每个 id ∈ [0, total_) 恰好被发出**一次**：多线程并发调用也不能重复、不能遗漏；
4. 不允许使用互斥锁（`std::mutex` 等）——本任务的用意就是无锁编程。

**Hint**：一次 `next_.fetch_add(1)` 就是全部答案。想清楚两件事：

1. `fetch_add` 返回的是**加之前的旧值**——这是分配给当前线程的 id
2. 当旧值已经 ≥ `total_` 时意味着发完了——**多个线程可能同时越过终点**，它们拿到的 id 互不相同但都 ≥ total，都返回 false 即可，这不是 bug

```
时间线示例（4 线程，total=3，只剩 1 个 morsel）：

  next_ = 2
  T0: fetch_add → old=2, next_=3  →  2<3 → morsel_id=2, return true  ✓
  T1: fetch_add → old=3, next_=4  →  3≥3 → return false
  T2: fetch_add → old=4, next_=5  →  4≥3 → return false
  T3: fetch_add → old=5, next_=6  →  5≥3 → return false

  恰好 morsel 2 被发了一次，之后所有人都返回 false。正确。
```

**内存序**：`std::memory_order_relaxed` 对本任务完全够用——我们只要求"每个 id 恰好发一次"这个原子性，不依赖原子变量去同步其他数据。对内存序感兴趣，推荐阅读 *C++ Concurrency in Action* 第 5 章。

**测试对照**：
- `SequentialDispatch`：单线程顺序派发，检查 id 从 0 开始连续
- `EmptyQueue`：total=0 时直接返回 false
- `ExhaustedQueueStaysExhausted`：发完之后再调 100 次都返回 false
- `ConcurrentExactlyOnce`：4 线程瓜分 10000 个 morsel，汇总排序后必须恰好是 0..9999（无重复无遗漏）
- `SingleProducerManyThreads`：8 线程抢 1 个 morsel，恰好 1 个线程成功

## Testing

```bash
make -j4              # 编译（macOS 用 -j4；Linux 可用 -j$(nproc)）
./tdbtest Lab0        # 只跑 Lab 0 的 5 个用例
./tdbtest             # 跑全部测试（此时只有 Lab 0 应该过，其余 Lab 的 TODO 桩会抛 NotImplementedException——正常）
```

预期输出：

```
[==========] Running 5 tests from 1 test suite.
[ RUN      ] Lab0MorselTest.SequentialDispatch
[       OK ] Lab0MorselTest.SequentialDispatch
...
[==========] 5 tests from 1 test suite ran.
[  PASSED  ] 5 tests.
```

## Development Hints

- 怀疑并发 bug 时，把测试里的 `TOTAL` 调大（如 100 万）、线程数调高（如 16），连跑几十次——竞态条件在高压力下几乎必然现形
- 不要为通过测试而引入全局锁——本 Lab 的验收点恰恰是**没有**锁
- 先让单线程测试过，再验证并发。单线程正确是并发正确的必要非充分条件，但单线程不对就不用想并发
- 写之前在纸上画一下"3 个 morsel、2 个线程"的 fetch_add 时间线，确认你理解"越界即终止"的语义

## Grading Rubric

1. `./tdbtest Lab0` 5 个用例全部通过（包括并发压力测试）；
2. 实现中不出现 `std::mutex`、`std::lock_guard` 等同步原语；
3. 编译零警告（项目开了 `-Wall -Wextra`）。

## 往下看

Lab0 完成后，你已经掌握了 DuckDB 并行调度的核心原语。接下来：

- **Lab 1（列式存储）**：构建行组、列块，实现 zone map——让 morsel 里有真实数据可读，并支持"整段跳过"的优化
- **Lab 2（SQL 前端）**：把 SQL 文本解析、绑定成物理计划——告诉执行引擎要扫哪些 morsel
- **Lab 3（执行引擎）**：你将在 `ParallelTableScanState` 里亲手写出和 MorselQueue 一模一样的 `fetch_add`，把 morsel 派发给工作线程；然后是 Filter、Aggregate、Join……全部基于 morsel 驱动

## 思考题（不计分）

1. 如果把 `fetch_add` 换成 `compare_exchange_weak` 循环，语义相同，性能差多少？为什么现代 CPU 上前者是单指令？（提示：x86 的 `lock xadd` vs CAS 循环的重试次数）
2. morsel 取多大合适？tiny-duckdb 取 2048 行，DuckDB 取 ~122000 行。太小会怎样（调度开销占比），太大会怎样（拖尾效应）？Lab 3 结尾的进阶练习会让你用真实查询回答这个问题。
3. 为什么 `ParallelTableScanState` 不直接复用你写的 `MorselQueue` 类，而是内联了一个 `std::atomic<idx_t> next_`？（提示：它发的不是整数 id，而是 `TableScanMorsel` 结构体——但核心模式完全相同。）
