# Lab 3 - 执行引擎：push-based、morsel-driven、向量化

> 对应代码：`src/execution/expression_executor.cpp`、`src/execution/operator/*.cpp`、`src/execution/pipeline.cpp`
> 对应测试：`test/lab3_execution_test.cpp`（36 个用例，端到端跑真实 SQL）

## Overview

这是整个课程的核心 Lab：把 Lab 1 的列存、Lab 2 的计划串成一台**真正并行**的查询引擎。完成后，`tiny_duckdb_shell` 里所有 SQL 都由你写的代码执行。

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| Task L3.T1 | ExpressionExecutor：表达式向量化求值（Evaluate/Select） | `./tdbtest Lab3ExecutionTest.ExpressionEvaluator` |
| Task L3.T2 | Filter + Projection：流水线中段算子 | `./tdbtest Lab3ExecutionTest.WhereFilter ProjectionArithmetic SelectStar` |
| Task L3.T3 | TableScan：morsel 驱动扫描 + zone map 下推 | `./tdbtest Lab3ExecutionTest.ZoneMapPrunedScan ParallelScan ScanEmpty` |
| Task L3.T4 | HashAggregate：两阶段并行聚合 | `./tdbtest Lab3ExecutionTest.CountStar SumAvg GroupBy Aggregate Parallel` |
| Task L3.T5 | HashJoin：build/probe + HAVE_MORE_OUTPUT 续传 | `./tdbtest Lab3ExecutionTest.Join` |
| Task L3.T6 | OrderBy + Limit：物化与截断 | `./tdbtest Lab3ExecutionTest.OrderBy Limit` |

**推荐顺序**（Road Map）：T1 → T2 → T3 后，简单 SELECT 已端到端可用；T4、T6 互不依赖；T5 最难，放最后。每完成一个任务，对应的端到端测试组就会成片转绿。

## Background

### 火山模型 vs push-based

教科书执行器是**火山模型**：每个算子提供 `Next()`，父算子向子算子"拉"一行。一行一次虚函数调用，还要把行从叶子一路传回根——CPU 全耗在调度上。现代引擎（DuckDB、Umbra、Velox）反过来：**source 把一整块数据向 sink 推**，算子按块处理：

```
 火山（pull，每行一次调用）            push-based（每块一次调用）
   Projection.Next() ─┐               TableScan ─push(chunk)→ Filter ─push→ Sink
   Filter.Next()      │ 逐行              └──── 一次 2048 行 ────┘
   Scan.Next()      ──┘
```

### 三种角色（source / operator / sink）

tiny-duckdb 的物理算子可以在一条流水线里扮演至多三种角色（`physical_operator.hpp`，**先读这个头文件**）：

| 角色 | 接口 | 例子 | 语义 |
|------|------|------|------|
| SOURCE | `GetData(chunk, input)` | TableScan；Finalize 后的聚合/排序/LIMIT | 产出数据块，空块 = 流结束 |
| OPERATOR | `Execute(chunk, state) -> OperatorResultType` | Filter、Projection、Join probe | 原地转换一个块 |
| SINK | `Sink / Combine / Finalize` | 聚合、排序、LIMIT、Join build、ResultCollector | 消费整条流，之后可转为 SOURCE |

`Execute` 的返回值：`NEED_MORE_INPUT`（输入已消费完，拉新块）或 `HAVE_MORE_OUTPUT`（**同一逻辑输入还有输出没吐完**，流水线不换输入再调一次——Join probe 一行配多行时必须用它，见 T5）。

### 流水线是怎么切出来的

逻辑计划被 `PipelineBuilder`（`src/execution/pipeline.cpp`，已提供，**务必精读**）切成若干条"一条 source → 若干 operator → 至多一个 sink"的流水线：

```
SELECT flag, count(*) FROM lineitem JOIN keys ON k = orderkey WHERE q > 1 GROUP BY flag

 pipeline 1: scan(keys)          ──► sink: HASH_JOIN (build 侧)
 pipeline 2: scan(lineitem) → filter(q>1) → join probe ──► sink: HASH_GROUP_BY
 pipeline 3: source: HASH_GROUP_BY (finalize 后) ──► sink: RESULT_COLLECTOR
```

切分点都在 **sink**：遇到一个 sink，它的子树先单独成一条流水线（孩子推向它），它自己 Finalize 后再当下一条流水线的 source。

### morsel-driven 并行

每条流水线的 source 把输入切成 morsel（TableScan 是"行组 × STANDARD_VECTOR_SIZE"切片，见 Lab 1 的 `ParallelTableScanState`），N 个线程循环领 morsel 各自推完整条流水线——线程之间**零协调**，直到 sink 的 `Combine` 才合并线程局部状态。执行循环（`Pipeline::Execute/ExecuteWorker`）已提供；工作线程的异常会被捕获并在主线程重抛，你的算子里抛异常不会炸掉进程。

### 向量化执行

所有计算以 Vector（2048 值）为单位：表达式求值一次性吃掉一个 chunk 产生一个 Vector，谓词一次性产生一个 SelectionVector。本实现为了可读性统一走 `Value` 装箱（比 DuckDB 的特化裸循环慢一个数量级，换来 150 行就能读完的求值器）；**类型特化是结尾的进阶练习**。

## Task #1 - ExpressionExecutor

`expression_executor.cpp` 的文件级注释是完整规格。要点：

- **T1a `Evaluate`**：对五类表达式（列引用/常量/比较/连接词/算术）递归求值。比较遇 NULL 得 NULL；连接词简化三值逻辑（NULL 视为 false，结果永不为 NULL）；算术直接用 `Value::Add` 等（它们自己传播 NULL）。比较分派已提供（`EvaluateComparison`）。
- **T1b `Select`**：布尔表达式 → 命中行号写入 SelectionVector，返回命中数。NULL 不命中。

**测试**：`ExpressionEvaluatorArithmetic`、`ExpressionEvaluatorSelectWithNulls`、`ExpressionEvaluatorConstant`、`ExpressionEvaluatorVarcharComparison`、`ExpressionEvaluatorConjunction`。

## Task #2 - Filter 与 Projection

两个中段算子，都是"拿到 chunk、原地变换、返回 `NEED_MORE_INPUT`"：

- **Filter**：用 T1b 的 `Select` 填 FilterState 的 SelectionVector，再 `chunk.Slice(sel, count)` 压实；全命中时跳过 Slice。state 是线程私有的——这就是为什么 SelectionVector 可以复用而无需加锁。
- **Projection**：逐列 `Evaluate` 到 ProjectionState 的暂存 chunk，`SetCardinality(输入行数)`，整体 move 出去，再给暂存 chunk 重新 `Initialize`。

**测试**：`WhereFilter`、`WhereConjunction`、`FilterNoRowsMatch`、`ProjectionArithmetic`、`ProjectionConstantColumns`、`SelectStar`。

## Task #3 - TableScan：morsel + zone map 下推

`GetData` 的循环：**领 morsel → zone map 检查 → 扫描或跳过**：

1. `scan_state->NextMorsel(morsel)` 为空时产出空块（结束信号）；
2. 对每个 `TableFilter`（计划生成器从 WHERE 里抽出的"列 比较 常量"），调 `table.CheckZoneMap(...)`——任一过滤条件证明整组不可能命中，**跳过该 morsel 继续领下一个**；
3. 未跳过则 `table.Scan(morsel, column_ids, chunk)`。

想清楚"所有 morsel 都被剪掉"时循环自然落到空块分支——这正是 `ZoneMapPrunedScanStillCorrect` 里 `l_orderkey > 100000` 的情形。裁剪只影响**读多少**，绝不影响**结果**：剩余谓词仍由下游 Filter 完整求值。

**测试**：`ZoneMapPrunedScanStillCorrect`、`ParallelScanConsistency`（1 线程 vs 4 线程结果必须逐值相同）、`ScanEmptyTable`。

## Task #4 - HashAggregate：两阶段并行聚合

最难的一个 sink，拆成两半：

**T4a 聚合状态机**（`AggregateState::Update/Merge/Finalize`）：每个分组每个聚合函数一份状态。`count(*)` 无参数；`count(col)` 跳过 NULL；sum/avg 累计 sum+count；min/max 用 `has_value` 记录"见过非 NULL"。`Finalize` 的语义细节（测试逐条查）：

- count 永远返回 `BigInt`，即使 0；
- sum/avg/min/max 在没有非 NULL 输入时返回**类型化 NULL**；
- sum 看返回类型：DOUBLE 列给 `Value::Double`，整型列给 `Value::BigInt`。

**T4b 两阶段协议**（`Sink/Combine/Finalize/GetData`）：

```
 线程 1 ──Sink──► 线程局部哈希表 ─┐
 线程 2 ──Sink──► 线程局部哈希表 ─┼─Combine(加锁)─► 全局哈希表 ─Finalize─► 结果行
 线程 3 ──Sink──► 线程局部哈希表 ─┘                                  └─GetData 分批吐出
```

- `Sink`：先**向量化**求值所有分组键和聚合参数（每个表达式整个 chunk 一次 Evaluate），再逐行进局部表（`FindOrCreate` + `Update`）；
- `Combine`：全局锁内 `FindOrCreate` + `Merge`；
- `Finalize`：每组产出一行 `[分组键..., 聚合结果...]`。**特例**：无 GROUP BY 的聚合空输入也必须产出恰好一行（`SELECT count(*) FROM empty → 0`）——`FindOrCreate({})` 强制造出那个"空分组"；
- `GetData`：操作符变 source，用 `emit_offset_.fetch_add` 把结果行分批（每批 ≤2048）分给各线程，发完产空块。

**测试**：`CountStar`、`AggregateEmptyTableNoGroupBy`、`SumAvgMinMax`、`GroupBySingleColumn`、`GroupByEmptyTable`、`AggregateCountColumnSkipsNull`、`AggregateMinMaxVarchar`、`ParallelGroupByConsistency`。

## Task #5 - HashJoin：build/probe 与续传

Join 一人分饰两角（回看 Background 的流水线切分图）：

**T5a build 侧**（sink）：`Sink` 向量化求值 build 键（`condition.second`），NULL 键的行**直接丢弃**（SQL：NULL 永不匹配），其余 `{key, row}` 存线程局部；`Combine` 汇总；`Finalize` 建哈希表 `key → 行号列表`（**重复键是常态**：一键多行）。

**T5b probe 侧**（operator）：`Execute` 求值 probe 键（`condition.first`）、查表、输出拼接行 `[probe 列..., build 列...]`。本 Lab 最微妙的点：**一个输入块可能产生超过 STANDARD_VECTOR_SIZE 行输出**（一行配千行），所以：

- 每次调用最多输出 STANDARD_VECTOR_SIZE 行；
- 没吐完就返回 `HAVE_MORE_OUTPUT`——流水线会**不换输入**再调你一次，从 `HashJoinProbeState` 的游标处继续；
- 吐完了返回 `NEED_MORE_INPUT`。

`JoinFanoutBeyondVectorSize`（3×5000=15000 行输出）就是这条路径的回归测试——漏了续传逻辑会静默丢数据而不是报错。

**测试**：`JoinSimple`、`JoinWithFilter`、`JoinGroupBy`、`JoinDuplicateKeys`、`JoinEmptyBuildSide`、`JoinLargeParallel`、`JoinFanoutBeyondVectorSize`。

## Task #6 - OrderBy 与 Limit

物化辅助函数（`MaterializeChunk/CombineRows/EmitRows`）已提供——读一遍，它们就是 T4 那套 local/global 模式的复用。

- **T6a `OrderBy::Finalize`**：按 `keys`（列号, ascending）多键稳定排序；`Value::LessThan` 定义 NULL 最小；DESC 交换比较方向。
- **T6b `Limit::Sink`**：LIMIT 与顺序无关，**全局**截断——全局锁内只保留前 `limit` 行，够了就整块丢弃。

**测试**：`OrderByAsc`、`OrderByDescLimit`、`OrderByMultipleKeys`、`OrderByGroupByResult`、`LimitOnly`、`LimitZeroAndBeyondTotal`。

## Testing

```bash
./tdbtest Lab3                              # 36 个端到端用例
./tdbtest Lab3ExecutionTest.Join            # 只跑 join 组
./tiny_duckdb_shell                         # 手工验收：README 里的示例查询
```

里程碑自检：T1-T3 完成后 `SELECT ... FROM ... WHERE ...` 应已可用；T4 后聚合可用；T5 后 join 可用；T6 后全部 36 个用例转绿。

## Development Hints

- **先单线程后多线程**：`db.SetThreads(1)`（shell 里 `.threads 1`）下把语义调对，再开 4 线程验证 `ParallelScanConsistency` / `ParallelGroupByConsistency`——并行 bug 和语义 bug 分开抓。
- shell 是最好的调试器：小表上手工复现，再对照测试。
- Join 续传出问题时的典型症状：行数对不上且**总是**丢尾部——打印每次 Execute 的 output_count 与 has_pending。

## Grading Rubric

1. `./tdbtest Lab3` 全部通过（含单/多线程一致性用例）；
2. 编译零警告；
3. 热路径（Sink/probe 循环）不允许出现锁——锁只能出现在 Combine / Limit::Sink / TableData::Append 里；
4. 不修改执行循环（pipeline.cpp）与测试。

## 进阶练习（对应 BusTub 的 Leaderboard，不计分）

1. **类型特化**：给 `ExpressionExecutor` 加一条"全 INT 且无 NULL"的 fast path：直接 `const int32_t *` 裸指针循环。用 `JoinLargeParallel` 级别的数据量对比耗时。
2. **morsel 大小实验**：把 STANDARD_VECTOR_SIZE 调成 128 / 8192，跑 ParallelScanConsistency 计时，解释曲线形状（调度开销 vs 负载均衡）。
3. **迟物化**：现在 join 输出把整行复制进哈希表。改为只存 (key, rowid)，输出时再回表取列——这就是 late materialization。

## 思考题（不计分）

- 为什么聚合的两阶段协议比"所有线程共用一个带锁哈希表"快得多？（提示：锁竞争 + 缓存行乒乓。）
- `HAVE_MORE_OUTPUT` 本质上解决了什么问题？火山模型是怎么处理同一行配多行的（回想 BusTub 的 NestedLoopJoinExecutor）？
- 我们的 ORDER BY 是全物化的。Top-N（ORDER BY + LIMIT）可以优化成什么？
