# Lab 3 - 执行引擎：Push-Based、Morsel-Driven、向量化

> 对应代码：`src/execution/expression_executor.cpp`、`src/execution/operator/*.cpp`、`src/execution/pipeline.cpp`
> 对应测试：`test/lab3_execution_test.cpp`（37 个用例，全部端到端跑真实 SQL）

## Overview

这是整个课程的**核心 Lab**。Lab 0 给了你 morsel 原语，Lab 1 给了你列存数据，Lab 2 给了你查询计划——Lab 3 把它们串成一台**真正并行**的查询引擎。完成后，`tiny_duckdb_shell` 里所有 SQL 都由你写的代码执行。

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| T1 | ExpressionExecutor：表达式向量化求值（Evaluate/Select） | `./tdbtest Lab3ExecutionTest.ExpressionEvaluator*` |
| T2 | Filter + Projection：流水线中段算子 | `./tdbtest Lab3ExecutionTest.WhereFilter ProjectionArithmetic SelectStar FilterNoRowsMatch` |
| T3 | TableScan：morsel 驱动扫描 + zone map 下推 | `./tdbtest Lab3ExecutionTest.ZoneMapPrunedScanStillCorrect ParallelScanConsistency ScanEmptyTable` |
| T4 | HashAggregate：两阶段并行聚合 | `./tdbtest Lab3ExecutionTest.CountStar SumAvgMinMax GroupBy Parallel Aggregate*` |
| T5 | HashJoin：build/probe + HAVE_MORE_OUTPUT 续传 | `./tdbtest Lab3ExecutionTest.Join*` |
| T6 | OrderBy + Limit：物化、排序与截断 | `./tdbtest Lab3ExecutionTest.OrderBy* Limit*` |

**推荐顺序**：T1 → T2 → T3 后，`SELECT ... FROM ... WHERE ...` 已端到端可用；T4（聚合）、T6（排序/截断）互不依赖；T5（Join）最微妙，放最后。每完成一个任务，对应的端到端测试组就会成片转绿。

---

## Background

### 0. 系统全景——Lab 3 在整个 tiny-duckdb 中的位置

```
┌─────────────────────────────────────────────────────────────────────┐
│                         SQL 文本 (Lab 2)                            │
│   "SELECT flag, COUNT(*) FROM t WHERE q > 1 GROUP BY flag"         │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ Binder + Planner (Lab 2)
                           ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       物理算子树 (Lab 2 产物)                        │
│   ResultCollector                                                   │
│     └─ HashAggregate(GROUP BY flag)                                 │
│          └─ Filter(q > 1)                                           │
│               └─ TableScan(t)                                       │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ PipelineBuilder (已提供)
                           ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     Pipeline DAG (你在本 Lab 驱动)                    │
│                                                                     │
│  Pipeline 1:  TableScan(t) → Filter(q>1) → HashAggregate(sink)     │
│  Pipeline 2:  HashAggregate(source) → ResultCollector(sink)        │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ Execute() 多线程并行
                           ▼
┌─────────────────────────────────────────────────────────────────────┐
│                  Morsel-Driven Push 执行 (Lab 0+1+3)                │
│                                                                     │
│   线程 0 ──► 领 morsel ─► Scan列存(Lab1) ─► 向量化表达式 ─► Sink    │
│   线程 1 ──► 领 morsel ─► Scan列存(Lab1) ─► 向量化表达式 ─► Sink    │
│   线程 2 ──► 领 morsel ─► Scan列存(Lab1) ─► 向量化表达式 ─► Sink    │
│                                          ... Combine → Finalize     │
└─────────────────────────────────────────────────────────────────────┘
```

**跨 Lab 串联**：
- Lab 0 的 `MorselQueue::NextMorsel` → 本 Lab 的 `ParallelTableScanState::NextMorsel`（T3），是同一个原子 `fetch_add` 模式的直系后代；
- Lab 1 的 `ColumnChunk`/zone map → 本 Lab TableScan 的 zone map 下推（T3）；
- Lab 2 的物理计划 → `PipelineBuilder::BuildRecursive` 自动切成流水线（框架已写好，你只需驱动算子）。

### 1. 数据单元：DataChunk / Vector / SelectionVector

在动手之前，必须先搞清楚执行引擎的数据流通"货币"：

```
DataChunk（一次 push 的数据单元，≤2048 行）
┌─────────────────────────────────────────────────┐
│ Vector #0  Vector #1  Vector #2  ...           │  ← N 列，等长
│  (INT)      (DOUBLE)   (VARCHAR)                │
│ ┌──────┐  ┌──────┐  ┌──────┐                    │
│ │  10  │  │ 3.14 │  │ "foo"│ ← row 0           │
│ │  20  │  │ 2.71 │  │ "bar"│ ← row 1           │
│ │  30  │  │ NULL │  │ "baz"│ ← row 2           │
│ │ ...  │  │ ...  │  │ ...  │                    │
│ │2048? │  │2048? │  │2048? │ ← 最多 STANDARD_VECTOR_SIZE 行      │
│ └──────┘  └──────┘  └──────┘                    │
│                                                 │
│ ValidityMask: 64-bit 位图，bit=0 表示该位置 NULL  │
└─────────────────────────────────────────────────┘
         ▲
         │ 过滤后
SelectionVector（行号数组，表示"哪些行活下来了"）
 ┌─────────────────┐
 │ [0, 2, 5, ...]  │ ← 压实行号，无需移动数据
 └─────────────────┘
```

**核心设计思想**：
- **Vector** = 一列 ≤2048 个同类型值 + ValidityMask（NULL 位图）；
- **DataChunk** = 若干等长 Vector 的集合，是算子间推数据的单位；
- **SelectionVector** = 一个 `sel_t[]` 数组，记录**逻辑行号**而非实际数据。Filter 不必移动内存里的数据，只需填写 SelectionVector 告诉下游"第 0、2、5 行活下来了"。

这就是向量化执行的关键：**批量处理 + 避免复制**。Filter 不复制行，Join 不逐行做函数调用。

**常量**（定义在 `src/include/tiny_duckdb/common/types.hpp`）：
- `STANDARD_VECTOR_SIZE = 2048` —— 一个 Vector/DataChunk 的最大行数；
- `ROW_GROUP_SIZE = 4 * STANDARD_VECTOR_SIZE = 8192` —— 列存行组大小（Lab 1）。

### 2. 火山模型 vs Push-Based

教科书的执行器是**火山模型（Volcano / Iterator Model）**：每个算子实现 `Next()`，父算子向子算子"拉"一行。

```
火山模型（pull）—— 每行一次虚函数调用 + 逐行拷贝
═════════════════════════════════════════════════

    ResultCollector.Next()
        │
        ▼
    Aggregate.Next()  ──── 虚函数调用 #N
        │
        ▼
    Filter.Next()     ──── 虚函数调用 #N
        │
        ▼
    TableScan.Next()  ──── 虚函数调用 #N
        │
        ▼  返回一行 (10, 3.14, "foo")
```

问题：
- 2048 行就要做 2048 × 3 = 6144 次虚函数调用；
- 每行都要在调用栈上传递，CPU 分支预测失败 + 缓存不友好；
- 很难并行：每个 `Next()` 都可能阻塞。

**Push-based（Morsel-Driven）**反过来：source 把整块数据向下游**推**，每个算子一次处理整个 chunk：

```
Push-based 模型 —— 每个 chunk 一次虚函数调用
═════════════════════════════════════════════

    TableScan ──push(chunk: 2048 行)──► Filter ──► Projection ──► Sink
                │                           │              │
                │ 一次调用处理 2048 行       │ 一次调用     │ 一次调用
                ▼                           ▼              ▼
              [列存]                    [SelectionVector] [局部哈希表]
```

**关键对比**：

| 维度 | 火山模型 | Push-based |
|------|---------|-----------|
| 调度单位 | 一行 | 一块（≤2048 行） |
| 每 2048 行虚函数调用 | ~6000 次 | ~3 次 |
| 缓存局部性 | 差（逐行跳转） | 好（顺序扫列） |
| 并行化 | 困难 | 天然（每条 pipeline 独立） |
| CPU 分支预测 | 频繁失败 | 循环内预测稳定 |

### 3. 三种算子角色：Source / Operator / Sink

tiny-duckdb 的每个物理算子可以在一条流水线里扮演至多三种角色（接口定义在 `src/include/tiny_duckdb/execution/physical_operator.hpp`）：

```
┌────────────────────────────────────────────────────────────────┐
│                       一条 Pipeline                            │
│                                                                │
│   ┌──────┐    ┌──────────┐    ┌──────────┐    ┌──────┐        │
│   │SOURCE│───►│OPERATOR 0│───►│OPERATOR 1│───►│ SINK │        │
│   └──────┘    └──────────┘    └──────────┘    └──────┘        │
│   GetData      Execute()       Execute()      Sink/Combine/   │
│  产出 chunk     变换 chunk     变换 chunk       Finalize       │
│                                                                │
│  TableScan      Filter         Join probe    HashAgg(build)   │
│  Agg(after      Projection                  Join(build)      │
│   Finalize)                                  OrderBy          │
│  OrderBy(after                               Limit            │
│   Finalize)                                  ResultCollector  │
│  Limit(after                                                   │
│   Finalize)                                                    │
└────────────────────────────────────────────────────────────────┘
```

| 角色 | 接口 | 返回/语义 |
|------|------|----------|
| **SOURCE** | `GetData(chunk, input)` | 产出 DataChunk；空 chunk（size=0）= 流结束 |
| **OPERATOR** | `Execute(chunk, state)` → `OperatorResultType` | 原地变换 chunk；返回 `NEED_MORE_INPUT`（输入已消费完，拉新块）或 `HAVE_MORE_OUTPUT`（同一逻辑输入还有输出没吐完，**不换输入**再调一次——T5 详述） |
| **SINK** | `Sink(...)` → `Combine(...)` → `Finalize(...)` | 消费整条流；Combine 合并线程局部状态；Finalize 后一些 sink（Agg/OrderBy/Limit）会**变成**下一条 pipeline 的 SOURCE |

**状态对象分层**（这是并行安全的关键）：

```
GlobalSourceState / GlobalSinkState
    └── 所有线程共享，只在 Combine 时加锁访问

LocalSinkState / OperatorState
    └── 每个线程一份，Sink/Execute 中无锁读写
```

```
Pipeline::ExecuteWorker 每线程建立的状态：
═════════════════════════════════════════

    Thread 0                           Thread 1
    ─────────                          ─────────
    source_state (OperatorState)       source_state
    operator_states[0..N]              operator_states[0..N]
    local_sink (LocalSinkState)        local_sink (LocalSinkState)

    所有线程共享：
    global_source (GlobalSourceState)
    global_sink  (GlobalSinkState)
```

为什么要分 global/local？因为并行执行时，各线程在 Sink 阶段写自己的 `LocalSinkState`（无锁），等所有线程结束后才在 `Combine` 里加锁合并到 `GlobalSinkState`——这是 Lab 0 "零协调并行"思想的延续。

### 4. Pipeline 是怎么切出来的

物理算子树被 `PipelineBuilder::BuildRecursive`（`src/execution/pipeline.cpp`，**已提供，请精读**）切成若干条 pipeline。规则是：

- **遇到 sink 算子**（HashAgg / HashJoin / OrderBy / Limit / ResultCollector）：
  - sink 的**子树**先单独构成一条 pipeline，把数据推给这个 sink；
  - 该 sink Finalize 后变成**下一条 pipeline 的 source**；
- **HashJoin 特殊**：右孩子（build 侧）单独成一条 pipeline（sink = Join，做 build），左孩子（probe 侧）继续当前 pipeline（Join 作为中段 operator 做 probe）；
- **TABLE_SCAN** 是 pipeline 叶子：设置为 source；
- **FILTER / PROJECTION** 是中段 operator：递归处理孩子，再把自己 push 进 operators 列表。

#### 具体例子

```sql
SELECT flag, COUNT(*)
FROM lineitem
JOIN keys ON k = orderkey
WHERE q > 1
GROUP BY flag
```

物理算子树：
```
ResultCollector
  └─ HashAggregate(GROUP BY flag, COUNT(*))
       └─ HashJoin(k = orderkey)          ◄── sink + operator 双角色
            ├─ (right) TableScan(keys)    ◄── build 侧
            └─ (left) Filter(q > 1)
                 └─ TableScan(lineitem)   ◄── probe 侧
```

PipelineBuilder 从上往下递归：

```
步骤 1: ResultCollector 是 sink
        → 处理孩子：HashAggregate 也是 sink → 先切孩子 pipeline
        → current_pipeline.sink = ResultCollector

步骤 2: HashAggregate 是 sink
        → 切一条孩子 pipeline：处理 HashJoin
        → 孩子 pipeline 的 sink = HashAggregate
        → current_pipeline.source = HashAggregate (Finalize 后产出数据)

步骤 3: HashJoin
        → 切 build pipeline（右孩子）：sink = HashJoin, source = TableScan(keys)
        → push build_pipeline 到 pipelines_ 列表
        → 处理左孩子：Filter + TableScan(lineitem)
        → Join 自己加入 operators 做 probe

步骤 4: Filter(q>1)
        → 递归处理 TableScan(lineitem) 作为 source
        → Filter 加入 operators

步骤 5: TableScan 是 source
        → child_pipeline.source = TableScan(lineitem)
```

最终得到 3 条 pipeline，**严格按顺序执行**（build 必须在 probe 之前完成；Agg 必须在 drain Agg source 之前 Finalize）：

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Pipeline 1 (build 侧，最先执行)                                         │
│   source: TableScan(keys)                                              │
│   operators: (无)                                                       │
│   sink: HashJoin (build hash table)                                    │
├─────────────────────────────────────────────────────────────────────────┤
│ Pipeline 2 (probe + 聚合)                                               │
│   source: TableScan(lineitem)                                          │
│   operators: [Filter(q>1), HashJoin(probe)]                            │
│   sink: HashAggregate (两阶段聚合)                                      │
├─────────────────────────────────────────────────────────────────────────┤
│ Pipeline 3 (输出结果，最后执行)                                         │
│   source: HashAggregate (Finalize 后按 offset 分批吐出)                │
│   operators: (无)                                                       │
│   sink: ResultCollector (收集结果给客户端)                              │
└─────────────────────────────────────────────────────────────────────────┘
         执行顺序：P1 → P2 → P3（pipeline 之间串行，pipeline 内并行）
```

> ⚠️ **关键理解**：Pipeline 之间是**串行**的（P1 必须完全结束并 Finalize，P2 才能启动；P2 必须 Combine+Finalize 完成，P3 才能 drain），但**每条 pipeline 内部是多线程并行**的。

### 5. 执行循环——一个 Worker 的一生

`Pipeline::ExecuteWorker`（`src/execution/pipeline.cpp` 第 63-111 行）是每个工作线程的入口。框架已写好，但你必须**读懂它**才能写对算子里的状态管理：

```
ExecuteWorker(context, global_source, global_sink)
│
├── 1. 创建本线程私有状态
│     source_state     ← source->GetOperatorState()
│     operator_states[i] ← operators[i]->GetOperatorState()   (每个中段算子一份)
│     local_sink       ← sink->GetLocalSinkState(global_sink) (线程局部 sink 状态)
│
├── 2. morsel 循环（直到 source 产空块）
│     while (true):
│       ├── chunk.Initialize(source->types)       // 重置 chunk schema
│       ├── source->GetData(chunk, input)          // 领 morsel，填 chunk
│       ├── if chunk.size() == 0: break            // 没有更多 morsel，退出
│       │
│       ├── operator 链推进（含 HAVE_MORE_OUTPUT 续传循环）
│       │   need_more_input = false
│       │   while (!need_more_input):
│       │     need_more_input = true
│       │     for i in 0..operators.size()-1:
│       │       result = operators[i]->Execute(chunk, *operator_states[i])
│       │       if result == HAVE_MORE_OUTPUT:
│       │         need_more_input = false     // 该算子还有输出，不换输入继续
│       │       if chunk.size() == 0:
│       │         break                        // 被下游过滤干净了
│       │     if chunk.size() > 0 and sink:
│       │       sink->Sink(chunk)              // 推给 sink
│       │
│       └── （循环继续：要么拉新 morsel，要么续传 HAVE_MORE_OUTPUT）
│
└── 3. morsel 吃完后
      sink->Combine(global_sink, local_sink)   // 把本线程的局部状态合并到全局
```

`Execute` 主函数做的事：
1. 创建 global_source 和 global_sink；
2. 启动 `thread_count - 1` 个 worker 线程，主线程也作为 worker 参与执行；
3. 异常捕获：worker 线程的异常会被收集，join 所有线程后在主线程重抛（防止 `std::terminate`）；
4. **所有 worker 都 Combine 之后**，才在主线程调 `sink->Finalize(global_sink)`——这是 Finalize 能看到完整全局数据的原因。

#### 时序图（4 线程、5 morsel、带 Aggregate sink）

```
时间 →
     主线程              Worker 1           Worker 2           Worker 3
────────────────────────────────────────────────────────────────────────
     建 global 状态
     启动 workers ─────►
     ExecuteWorker     ExecuteWorker      ExecuteWorker      ExecuteWorker
       │                  │                  │                  │
       ▼                  ▼                  ▼                  ▼
     morsel=0           morsel=1           morsel=2           morsel=3
     扫→Filter→AggSink  扫→Filter→AggSink  扫→Filter→AggSink  扫→Filter→AggSink
       │                  │                  │                  │
       ▼                  ▼                  ▼                  ▼
     morsel=4           (空)退出           (空)退出           (空)退出
     扫→Filter→AggSink
       │
       ▼
     (空)退出
       │
       ├──── Combine ──── Combine ───────── Combine ───────── Combine
       │              (加锁合并 local→global)
       ▼
     Finalize(global_sink)  ◄── 主线程独占执行
       │
       ▼
     Pipeline 完成
```

注意：每个线程**领 morsel 的顺序是不确定的**（取决于调度），但每个 morsel 恰好被处理一次（Lab 0 的原子保证）。

### 6. HAVE_MORE_OUTPUT：续传机制（T5 核心）

普通算子（Filter、Projection）一次 Execute 就消费完输入，返回 `NEED_MORE_INPUT`。但 **HashJoin probe** 不同：一个 probe 行可能匹配 build 侧多行（一对多、多对多），导致一个 2048 行的 probe chunk 可能产生 **超过 STANDARD_VECTOR_SIZE 行** 的输出。

举个具体例子：
- probe chunk 只有 3 行（`id=1, 2, 3`）；
- build 侧 `id=1` 有 5000 行，`id=2` 有 0 行，`id=3` 有 10000 行；
- 总共应输出 15000 行，但每次 Execute 最多吐出 2048 行。

此时 Join probe 必须"记住"处理到哪里了，下次被调用时**从断点继续**，而不是重新从 source 拉新 chunk。这就是 `HAVE_MORE_OUTPUT` 的意义：

```
HAVE_MORE_OUTPUT 续传流程（JoinFanoutBeyondVectorSize 测试场景）
═══════════════════════════════════════════════════════════════

HashJoinProbeState 里保存游标：
  - probe_chunk: 当前正在处理的 probe chunk（引用）
  - probe_row_idx: 当前 probe 行号
  - build_match_idx: 当前 probe 行匹配到 build 侧的第几个匹配

第一次 Execute(probe_chunk=[row0,row1,row2]):
  row0(id=1): 匹配 5000 行，输出前 2048 行
    → 游标: probe_row_idx=0, build_match_idx=2048
    → 返回 HAVE_MORE_OUTPUT

第二次 Execute(同一个 probe_chunk，不换输入!):
  row0(id=1): 继续输出剩余 2952 行中的 2048 行
    → 游标: probe_row_idx=0, build_match_idx=4096
    → 返回 HAVE_MORE_OUTPUT

第三次 Execute:
  row0(id=1): 输出最后 904 行
  row1(id=2): 无匹配，跳过
  row2(id=3): 匹配 10000 行，输出 1144 行（凑满 2048）
    → 游标: probe_row_idx=2, build_match_idx=1144
    → 返回 HAVE_MORE_OUTPUT

... 继续续传 ...

第七次 Execute:
  row2(id=3): 输出最后几行，全部匹配耗尽
    → 返回 NEED_MORE_INPUT  ← probe_chunk 处理完毕，拉新 chunk
```

执行循环里对 `HAVE_MORE_OUTPUT` 的处理（`pipeline.cpp` 第 89-106 行）：

```cpp
need_more_input = true;
for (idx_t i = 0; i < operators.size(); i++) {
    auto result = operators[i]->Execute(context, chunk, *operator_states[i]);
    if (result == OperatorResultType::HAVE_MORE_OUTPUT) {
        need_more_input = false;  // 记住：哪怕下游过滤空了也要续传
    }
    if (chunk.size() == 0) {
        break;  // 当前 chunk 被过滤干净，不需要续传
    }
}
if (chunk.size() > 0 && sink) {
    sink->Sink(context, *global_sink, *local_sink, chunk);
}
// 如果 need_more_input == false，外层 while 循环会再跑一次，
// 但不调用 source->GetData（chunk 保留），而是从 operators[0] 开始重新 Execute
```

> ⚠️ **容易错的点**：注释里特别强调——即使下游 Filter 把 chunk 清空了（`chunk.size() == 0`），只要某个上游算子返回了 `HAVE_MORE_OUTPUT`，`need_more_input` 仍然要设为 `false`。因为该算子**内部还有状态**（待续传的输出），必须再调一次让它吐完，否则会静默丢数据。`JoinFanoutBeyondVectorSize` 就是这条路径的回归测试。

---

## Task #1 - ExpressionExecutor

**文件**：`src/execution/expression_executor.cpp`

表达式求值器是所有计算的基础。Filter 的谓词、Projection 的列、Aggregate 的参数、Join 的键——都通过它求值。

`expression_executor.cpp` 的文件级注释是完整规格。两个静态方法：

### T1a `Evaluate(expr, chunk, result)`

递归对一个表达式树求值，结果写入 `result` Vector（大小 = chunk.size()）。五类表达式：

| 表达式类型 | 求值方式 |
|-----------|---------|
| BoundColumnRef | 从 chunk.GetVector(col_idx) 拷贝（或直接引用）对应列 |
| BoundConstant | 把常量 Value 填入 result 的每一行 |
| BoundComparison (`<`, `=`, `>`, ...) | 先递归 Evaluate 左右子节点，逐行比较 |
| BoundConjunction (`AND`, `OR`) | 递归 Evaluate 子节点，按三值逻辑合并 |
| BoundArithmetic (`+`, `-`, `*`, `/`) | 递归 Evaluate 子节点，逐行调用 `Value::Add` 等 |

**NULL 语义简化规则**（方便教学，非标准 SQL 完整三值逻辑）：
- 比较中任一操作数为 NULL → 结果为 NULL；
- 连接词（AND/OR）中 NULL **视为 false**，连接词结果**永不为 NULL**（简化设计）；
- 算术运算由 `Value::Add/Subtract/Multiply/Divide` 自行传播 NULL；
- 类型不匹配时直接抛异常。

**实现提示**：递归遍历表达式树，叶子节点（列引用/常量）直接填 result，内部节点先算左右孩子再合并。

### T1b `Select(expr, chunk, sel)`

把一个**布尔表达式**作用在 chunk 上，把结果为 true 的行号写入 `sel`（SelectionVector），返回命中行数。

- 结果为 NULL 的行**不命中**（WHERE 子句中 NULL 不满足条件）；
- 结果为 false 的行不命中；
- 只有 true 的行写入 sel；
- 可以先 Evaluate 成一个 BOOLEAN Vector，再遍历每行，把有效且为 true 的行号压入 sel。

**测试**：`ExpressionEvaluatorArithmetic`、`ExpressionEvaluatorSelectWithNulls`、`ExpressionEvaluatorConstant`、`ExpressionEvaluatorVarcharComparison`、`ExpressionEvaluatorConjunction`。

---

## Task #2 - Filter 与 Projection

两个标准的中段算子（OPERATOR 角色），都是"拿到 chunk、变换、返回 `NEED_MORE_INPUT`"。

### T2a PhysicalFilter

**文件**：`src/execution/operator/physical_filter.cpp`

Filter 的逻辑：
1. 从 OperatorState 取出一个**线程私有**的 SelectionVector（每个线程一份，无需加锁）；
2. 用 T1b 的 `ExpressionExecutor::Select` 把满足条件的行号填进 SelectionVector；
3. 如果命中数 == chunk.size()（全命中），直接返回 `NEED_MORE_INPUT`（无需压缩）；
4. 如果命中数 == 0，调用 `chunk.SetCardinality(0)` 清空；
5. 否则调 `chunk.Slice(sel, count)` 按 SelectionVector 压实行数据。

```
Filter 执行示意（q > 1）：
─────────────────────────────────
输入 chunk:
  q:     [1, 3, 0, 5, 2, NULL]
  flag:  [A, B, C, D, E, F]

Select 产生 sel: [1, 3, 4] (值为 true 的行号)

Slice 后输出 chunk:
  q:     [3, 5, 2]
  flag:  [B, D, E]
```

### T2b PhysicalProjection

**文件**：`src/execution/operator/physical_filter.cpp`（Filter 和 Projection 都在这里）

Projection 的逻辑：
1. 从 OperatorState 取出一个**线程私有**的暂存 DataChunk（`result_chunk`）；
2. `result_chunk.Initialize(output_types)` 重置；
3. 对每个投影表达式调用 `ExpressionExecutor::Evaluate`，结果写入 `result_chunk` 对应列；
4. `result_chunk.SetCardinality(chunk.size())`；
5. 把 `result_chunk` 的内容**移动/替换**到输入 chunk（因为 Projection 改变了列数和类型）；
6. 重置 `result_chunk` 的内部状态供下次使用。

**注意**：Projection 改变了 chunk 的 schema（列数可能减少、类型可能变化），因此执行循环里每次 `source->GetData` 之前都会 `chunk.Initialize(source->types)` 重置 schema。

**测试**：`WhereFilter`、`WhereConjunction`、`FilterNoRowsMatch`、`ProjectionArithmetic`、`ProjectionConstantColumns`、`SelectStar`。

---

## Task #3 - TableScan：Morsel + Zone Map 下推

**文件**：`src/execution/operator/physical_table_scan.cpp`

TableScan 是 SOURCE 角色。`GetData` 实现一个核心循环：

```
GetData(chunk, input):
  while (true):
    1. if !scan_state->NextMorsel(morsel):    // Lab 0 fetch_add 模式
         chunk.SetCardinality(0)              // 没有更多 morsel，产空块
         return

    2. pruned = false
       for each filter in table_filters:      // zone map 下推
         if !table.CheckZoneMap(morsel, filter.column_id,
                                filter.comparison, filter.constant):
           pruned = true
           break
       if pruned:
         continue                              // 整组不可能命中，跳过

    3. table.Scan(morsel, column_ids, chunk)  // 真正读数据
       return
```

### Zone map 下推细节

`table_filters` 是 Planner 从 WHERE 子句中抽出的"列 比较 常量"谓词（如 `q > 1`）。对于每个 filter，调 `table.CheckZoneMap` 判断当前 morsel 所在的 ColumnChunk 是否**可能**包含满足条件的值：

```
score 列的 ColumnChunk zone map: min=10, max=85

谓词          CheckZoneMap 结果    动作
─────────────────────────────────────────────
score > 90    false (max=85≤90)    跳过 morsel ✂️
score = 5     false (5<min=10)     跳过 morsel ✂️
score > 0     true  (有交集)        扫描
score >= 10   true  (10=min 有交集) 扫描
score < 10    false (min=10≥10)    跳过 morsel ✂️
```

**正确性原则**：Zone map 跳过只影响"读多少"，绝不影响"结果对不对"。跳过是一种优化——如果 zone map 说"不可能有"，就跳过；如果说"可能有"，就读进来，交给下游 Filter 精确判断。**假阳性（不该读的读了）只是慢一点，假阴性（该读的没读）就是错**。

具体的判断真值表（min=该 ColumnChunk 中该列最小值，max=最大值，c=常量）：

| 谓词 | 整个 chunk 可跳过（返回 false）当且仅当 |
|------|--------------------------------------|
| `col > c` | `max <= c` |
| `col >= c` | `max < c` |
| `col < c` | `min >= c` |
| `col <= c` | `min > c` |
| `col = c` | `c < min \|\| c > max` |
| `col != c` | **永不可跳过**（!= 不能 zone map 剪枝） |

> 💡 这正是 Lab 1 里 Zone map 的核心价值在执行引擎中的体现。

### Projection Pushdown（列裁剪）

`column_ids` 参数是 Planner 根据 SELECT 列表确定的"需要哪些列"。TableScan **只读取这些列**的数据，跳过其他列的 ColumnChunk。比如：

```sql
SELECT name, score FROM students WHERE age > 18
```

students 表有 5 列 (id, name, age, score, grade)，但 `column_ids = [1, 3]`（name 和 score），TableScan 只读这两列的 ColumnChunk——这是**投影下推（projection pushdown）**，是列存系统相对于行存的核心优势之一。

**测试**：`ZoneMapPrunedScanStillCorrect`（所有行被剪但结果正确——返回空集）、`ParallelScanConsistency`（1 线程 vs 4 线程结果必须逐值相同）、`ScanEmptyTable`。

---

## Task #4 - HashAggregate：两阶段并行聚合

**文件**：`src/execution/operator/physical_hash_aggregate.cpp`

这是本 Lab 最难的一个 sink，也是现代并行数据库聚合的标准做法。

### 为什么两阶段？

naive 做法：N 个线程共享一个加锁哈希表。问题：
- **锁竞争**：每个线程每处理一行都要抢锁，并行度退化为串行；
- **缓存行乒乓（cache line bouncing）**：多个线程的 CPU 核心反复使彼此的缓存行失效。

两阶段做法：

```
两阶段并行聚合（Two-Phase Aggregation）
═══════════════════════════════════════

   线程 0:  Sink → [局部 HT 0]  ─┐
   线程 1:  Sink → [局部 HT 1]  ─┼─Combine(加锁)──→ [全局 HT]  ──Finalize──→ 结果行
   线程 2:  Sink → [局部 HT 2]  ┘                    ▲
   线程 3:  Sink → [局部 HT 3]  ─────────────────────┘
               ...                                  │
              无锁写入                      加锁合并 N 次（不是 N×行数 次！）
```

Sink 阶段各线程写自己的局部哈希表（完全无锁），Combine 阶段只加锁 N 次（每个线程一次），合并局部表到全局表。锁的粒度从"每行列"降为"每线程一次"。

### 聚合状态机

每个分组（GROUP BY 的 key 组合）每个聚合函数维护一个 `AggregateState`。五种聚合函数的状态与操作：

| 聚合 | Update(单行输入) | Merge(另一状态合并) | Finalize(→输出 Value) |
|------|-----------------|-------------------|---------------------|
| `COUNT(*)` | count++ | count += other.count | Value::BigInt(count) |
| `COUNT(col)` | if not NULL: count++ | count += other.count | Value::BigInt(count) |
| `SUM(col)` | if not NULL: sum += v; n++ | sum += other.sum; n += other.n | n>0 ? Value(sum类型) : NULL |
| `AVG(col)` | if not NULL: sum += v; n++ | sum += other.sum; n += other.n | n>0 ? Value::Double(sum/n) : NULL |
| `MIN(col)` | if not NULL: if v<min or !has: min=v; has=true | if other.has and (!has or other.min<min): min=other.min | has ? Value(min) : NULL |
| `MAX(col)` | if not NULL: if v>max or !has: max=v; has=true | if other.has and (!has or other.max>max): max=other.max | has ? Value(max) : NULL |

**Finalize 语义细节**（测试逐条检查）：
- `COUNT(*)` 和 `COUNT(col)` 永远返回 `BigInt`，即使是 0（没有非 NULL 输入也返回 0）；
- `SUM/AVG/MIN/MAX` 在**没有非 NULL 输入**时返回对应类型的 NULL；
- `SUM` 的返回类型：输入是 DOUBLE 返回 DOUBLE，输入是整型返回 BigInt；
- `AVG` 总是返回 DOUBLE。

### T4b 两阶段协议实现

**Sink（无锁，线程局部）**：
1. 向量化求值 GROUP BY key 表达式（整个 chunk 一次 Evaluate，逐行取 Value 组成 key vector）；
2. 向量化求值聚合参数表达式（每个聚合函数一个 Evaluate）；
3. 逐行：用 key 在局部哈希表中 `FindOrCreate`，对返回的 AggregateState 调 `Update`。

**Combine（加锁，每线程一次）**：
1. 获取全局锁；
2. 遍历局部哈希表的每个 key，在全局表中 `FindOrCreate`；
3. 对每个分组调 `Merge(local_state, global_state)`。

**Finalize（主线程独占）**：
1. 遍历全局哈希表的每个分组；
2. 调每个聚合函数的 `Finalize` 得到输出 Value；
3. 组装结果行 `[group_key_vals..., agg_results...]`，存入 `result_rows_`；
4. 重置 `emit_offset_ = 0`。
5. **无 GROUP BY 的空输入特例**：`SELECT COUNT(*) FROM empty_table` 必须返回一行 `[0]`！如果全局哈希表为空，要强制造出一个空 key 的分组（`FindOrCreate({})`）并 Finalize 它。

**GetData（变 source，多线程 drain）**：
- 用 `emit_offset_.fetch_add(STANDARD_VECTOR_SIZE)` 原子领取一批结果行（每批 ≤2048）；
- 从 `result_rows_[offset .. offset+2048]` 组装 DataChunk；
- offset 超出行数时产空块。

```
Finalize 后 result_rows_ 内存布局：
┌──────┬───────┬───────┬───────┐
│ key0 │ agg0  │ agg1  │ ...   │  ← row 0
├──────┼───────┼───────┼───────┤
│ key1 │ agg0  │ agg1  │ ...   │  ← row 1
├──────┼───────┼───────┼───────┤
│ ...  │ ...   │ ...   │ ...   │
└──────┴───────┴───────┴───────┘
   ▲              ▲
  group key     聚合结果
  列(可变)      列(可变)
```

**测试**：`CountStar`、`AggregateEmptyTableNoGroupBy`、`SumAvgMinMax`、`GroupBySingleColumn`、`GroupByEmptyTable`、`AggregateCountColumnSkipsNull`、`AggregateMinMaxVarchar`、`ParallelGroupByConsistency`（1 线程 vs 4 线程结果必须一致，顺序无关）。

---

## Task #5 - HashJoin：Build/Probe 与续传

**文件**：`src/execution/operator/physical_hash_join.cpp`

HashJoin 一人分饰两角（回看第 4 节的 Pipeline 切分图）：
- **Pipeline 1**：右孩子（build 侧）→ Join 作为 SINK（构建哈希表）；
- **Pipeline 2**：左孩子（probe 侧）→ Filter/... → Join 作为 OPERATOR（探查哈希表）。

### T5a Build 侧（SINK 角色）

**Sink（线程局部）**：
1. 向量化求值 build 键（`condition.second`，即右孩子上的连接键表达式）；
2. 逐行：
   - 如果任何一个建键列为 NULL，**直接丢弃该行**（SQL 语义：NULL ≠ NULL，永不匹配）；
   - 否则，把整行数据（所有 build 列的 Value）和键存入线程局部的 build_rows 列表；
3. 记录每行在列表中的索引。

**Combine（加锁）**：
- 把线程局部的 build_rows 合并到全局 `build_rows_` 向量。

**Finalize（主线程独占）**：
- 遍历 `build_rows_`，构建真正的哈希表 `hash_table_: key → vector<row_idx>`；
- 注意**重复键是常态**：`key → [idx0, idx1, idx2, ...]`，一个键可能对应多行。

```
Build 侧完成后：
┌─────────────────────────────────────────────┐
│ build_rows_ (扁平化存储，下标即行号)          │
│  [0] key={k:1}, row={orderkey:1, name:"A"}  │
│  [1] key={k:1}, row={orderkey:1, name:"B"}  │
│  [2] key={k:2}, row={orderkey:2, name:"C"}  │
│  ...                                         │
└─────────────────────────────────────────────┘
     ▲
     │ 建索引
hash_table_:
  {1} → [0, 1, 500, 501, ...]   ← 一键多行
  {2} → [2, 3, ...]
  {3} → [4, ...]
  ...
```

### T5b Probe 侧（OPERATOR 角色 + HAVE_MORE_OUTPUT）

Probe 是本 Lab 最微妙的部分，也是 `HAVE_MORE_OUTPUT` 唯一需要返回的算子。

**HashJoinProbeState 必须包含**：
- 当前正在处理的 probe chunk（或其中已求值好的 probe 键）；
- 当前处理到 probe chunk 的第几行（`probe_row_idx`）；
- 当前 probe 行匹配到 build 侧的第几个匹配（`build_match_idx`）；
- 输出 buffer（攒满 STANDARD_VECTOR_SIZE 行后替换 chunk）。

**Execute 算法**：

```
Execute(chunk, state):
  if state 没有 pending 输出:
    1. 向量化求值 probe 键（condition.first，即左孩子的连接键表达式）
    2. 把 probe chunk/键存入 state，重置 probe_row_idx=0, build_match_idx=0

  output_count = 0
  while output_count < STANDARD_VECTOR_SIZE:
    if probe_row_idx >= chunk.size():
      // probe chunk 全部处理完毕
      把 output buffer 填充到 chunk，设置 cardinality = output_count
      return NEED_MORE_INPUT

    获取当前 probe 行的键
    if 键含 NULL:
      probe_row_idx++; build_match_idx = 0; continue  // NULL 不匹配

    在 hash_table_ 中查找
    if 未找到:
      probe_row_idx++; build_match_idx = 0; continue

    matches = hash_table_[key]  // build 侧所有匹配行的索引列表
    while build_match_idx < matches.size() and output_count < STANDARD_VECTOR_SIZE:
      build_row_idx = matches[build_match_idx]
      拼接输出行: [probe 行的所有列..., build_rows_[build_row_idx] 的所有列...]
      写入 output buffer
      build_match_idx++; output_count++

    if build_match_idx >= matches.size():
      probe_row_idx++; build_match_idx = 0;

  // output_count == STANDARD_VECTOR_SIZE，还有未输出的匹配
  把 output buffer 填充到 chunk，设置 cardinality = STANDARD_VECTOR_SIZE
  return HAVE_MORE_OUTPUT
```

**输出列的顺序**：probe 列（左孩子所有列）在前，build 列（右孩子所有列）在后。Planner 已经在 PhysicalHashJoin 构造时安排好了 `probe_types` 和 `build_types`。

**JoinFanoutBeyondVectorSize 测试场景**（3 个 probe 行产生 15000 行输出）前面已经详细展示了续传过程。如果漏了 HAVE_MORE_OUTPUT 逻辑，不会崩溃，但会**静默丢数据**——这是最危险的 bug 类型。

**测试**：`JoinSimple`、`JoinWithFilter`、`JoinGroupBy`、`JoinDuplicateKeys`、`JoinEmptyBuildSide`、`JoinLargeParallel`、`JoinFanoutBeyondVectorSize`。

---

## Task #6 - OrderBy 与 Limit

**文件**：`src/execution/operator/physical_order_by.cpp`

OrderBy 和 Limit 都是 **SINK + SOURCE**：必须看到所有输入才能产生输出（无法流式处理）。物化辅助函数（`MaterializeChunk/CombineRows/EmitRows`）已提供——**读一遍**，它们复用了 T4 聚合的 local/global 模式。

### T6a OrderBy::Finalize

Finalize 在所有数据 Combine 到全局后调用。任务：
1. 取出全局物化的所有行 `vector<vector<Value>>`；
2. 按 `keys`（vector of (列号, ascending bool)）做**多键稳定排序**；
3. `Value::LessThan` 定义了 NULL 最小（NULL 排在非 NULL 值前面）；
4. DESC 排序时交换比较方向（即 `LessThan` 取反）；
5. 排好序后存入 `result_rows_`，重置 `emit_offset_ = 0`。

```
多键排序示例 (ORDER BY flag ASC, score DESC):
─────────────────────────────────────────────
  输入行:
    (B, 90)   (A, 85)   (A, 95)   (B, 80)   (C, 100)

  按 flag ASC 先排:
    (A, 85)   (A, 95)   (B, 90)   (B, 80)   (C, 100)

  flag 相同的按 score DESC 稳定排序:
    (A, 95)   (A, 85)   (B, 90)   (B, 80)   (C, 100)
```

### T6b Limit::Sink

Limit 与顺序无关，**全局截断**——不需要等所有数据到齐，可以提前截断：

1. 获取全局锁；
2. 如果全局已收行数 >= limit：整块丢弃（什么都不做）；
3. 否则：计算还能收多少行（`remaining = limit - global_rows.size()`）；
4. 从输入 chunk 中最多取 `remaining` 行，追加到全局 result_rows_。

**注意**：Limit 的 Combine 和 Finalize 相对简单——Combine 不需要合并（因为 Limit::Sink 已经直接写全局了，它的 LocalSinkState 可能为空），Finalize 只需重置 emit_offset_。但要保证**框架约定的接口语义**，请参考已提供的 MaterializeChunk/CombineRows 辅助函数模式。

### 为什么 MaxSourceThreads() == 1？

OrderBy 和 Limit 都重写了 `MaxSourceThreads() const { return 1; }`（`physical_order_by.hpp` 第 33 行和第 54 行）。这是为什么？

如果允许多个线程并发 `GetData`：
- 线程 0 原子领到 offset=0，吐出 chunk[0..2047]；
- 线程 1 原子领到 offset=2048，吐出 chunk[2048..4095]；
- 但线程 1 可能比线程 0 **先完成**并先推给下游 sink！

```
MaxSourceThreads() == N (允许多线程 drain)：
═════════════════════════════════════════
线程 0: offset=0   → 组装 chunk[0..2047]   ────→ sink（晚到）
线程 1: offset=2048→ 组装 chunk[2048..4095] ──→ sink（早到！）

结果：sink 先收到 chunk[2048..4095]，后收到 chunk[0..2047]
     → 排序结果乱序！Top-K 错误！
```

对于 Aggregate 的 GetData，分组行之间没有顺序要求，多线程并发 drain 没问题。但 OrderBy 和 Limit 的输出**必须严格按全局顺序**，因此强制单线程 drain。

框架在 `Pipeline::Execute` 第 18 行尊重了这个约束：
```cpp
thread_count = std::max<idx_t>(1, std::min(thread_count, source->MaxSourceThreads()));
```

**测试**：`OrderByAsc`、`OrderByDescLimit`、`OrderByMultipleKeys`、`OrderByGroupByResult`、`OrderByPreservesOrderAcrossChunks`、`OrderByLimitUsesGlobalOrderAcrossChunks`、`LimitOnly`、`LimitZeroAndBeyondTotal`。

---

## Testing

```bash
make -j4                    # 编译
./tdbtest Lab3              # 37 个端到端用例全跑
./tdbtest Lab3ExecutionTest.Join            # 只跑 join 组（7 个用例）
./tdbtest Lab3ExecutionTest.ParallelGroupByConsistency  # 单个用例
./tiny_duckdb_shell         # 手工验收：README 里的示例查询
```

### 里程碑自检

| 完成 | 可用 SQL | 验证 |
|------|---------|------|
| T1 | 表达式求值（基础） | ExpressionEvaluator* |
| T1-T2 | `SELECT 1+2, a*3 FROM t` | SelectStar, ProjectionArithmetic |
| T1-T3 | `SELECT ... FROM t WHERE ...` | WhereFilter, ZoneMapPrunedScanStillCorrect |
| T1-T4 | `SELECT flag, COUNT(*) FROM t GROUP BY flag` | CountStar, GroupBySingleColumn |
| T1-T5 | `SELECT ... FROM t JOIN s ON ...` | JoinSimple, JoinWithFilter |
| T1-T6 | 全部 SQL（含 ORDER BY/LIMIT） | 全部 37 个用例 |

---

## Development Hints

1. **先单线程后多线程**：`db.SetThreads(1)`（shell 里 `.threads 1`）下调对语义，再开 4 线程验证 `ParallelScanConsistency` / `ParallelGroupByConsistency`——并行 bug 和语义 bug 分开抓。
2. **shell 是最好的调试器**：小表手工构造数据复现问题，再对照测试。
3. **Join 续传 bug 典型症状**：行数对不上且**总是丢尾部**——打印每次 Execute 的 output_count 和 probe/build 游标。
4. **Aggregate 空表无 GROUP BY 的特例**：`SELECT COUNT(*) FROM empty_table` 必须返回 0，不是空结果。Finalize 里检查全局哈希表是否为空。
5. **NULL 处理三原则**：(1) 比较遇 NULL 得 NULL；(2) WHERE 中 NULL 不命中；(3) Join 键 NULL 直接丢弃。

---

## Grading Rubric

1. `./tdbtest Lab3` 全部 37 个用例通过（含单/多线程一致性用例）；
2. 编译零警告；
3. 热路径（Sink/probe 循环）不允许出现锁——锁只能出现在 Combine / Limit::Sink / TableData::Append 里；
4. 不修改执行循环（`pipeline.cpp`）与测试文件。

---

## 进阶练习（不计分）

1. **类型特化 fast path**：给 `ExpressionExecutor` 加一条"全 INT 且无 NULL"的 fast path：直接 `const int32_t *` 裸指针循环加减乘除。用 `JoinLargeParallel` 级别的数据量对比耗时。
2. **Morsel 大小实验**：把 `STANDARD_VECTOR_SIZE` 调成 128 / 2048 / 8192，跑 `ParallelScanConsistency` 计时，解释曲线形状（调度开销 vs 缓存局部性 vs 负载均衡）。
3. **迟物化（Late Materialization）**：现在 Join build 侧把整行 `vector<Value>` 存在哈希表里。改为只存 `(key, rowid)`，probe 输出时再回表取列——减少哈希表内存占用和 build 时间。
4. **Top-N 优化**：当前 ORDER BY + LIMIT 全物化排序后截断。实现一个固定大小堆，内存 O(limit) 而非 O(total_rows)。

---

## 思考题（不计分）

- 为什么两阶段聚合比"所有线程共用一个带锁哈希表"快得多？估计一下在 4 线程、每个线程处理 100 万行时，全局锁方案的锁 acquire/release 次数 vs 两阶段方案的。
- `HAVE_MORE_OUTPUT` 本质上解决了什么问题？火山模型是怎么处理同一行配多行的？（回想 BusTub 的 NestedLoopJoinExecutor：它在 `Next()` 里保存了什么状态？）
- 我们的 ORDER BY 是全物化的。如果查询是 `SELECT * FROM t ORDER BY score LIMIT 10`，全物化排序的浪费在哪里？Top-N 堆优化的最坏/最好时间复杂度是多少？
- Pipeline 之间为什么必须串行执行？能不能让 Pipeline 2 的 probe 边和 Pipeline 1 的 build 边**流水线化**（pipeline parallelism）？需要什么前提？
- Zone map 下推中，如果表有多个 ColumnChunk 且数据排序，zone map 能跳过更多数据。如果数据完全随机呢？跳过率是多少？
