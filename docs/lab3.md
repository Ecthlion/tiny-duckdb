# Lab 3 - 执行引擎：push-based、morsel-driven、向量化

> 对应代码：`src/execution/`
> 测试：`test/lab3_execution_test.cpp`

这是整个课程的核心 Lab。三个关键词对应 DuckDB 执行器的三根支柱。

## 1. 向量化执行（L3.T1 - T2）

执行引擎里流动的最小单位不是行，而是 **DataChunk**（一批 ≤2048 行的列向量）：

- 每个算子一次处理一整块，函数调用/虚函数开销被摊薄到 1/2048；
- 内层循环遍历一列连续内存，编译器可以自动向量化（SIMD）；
- 代价模型简单：**一切优化都是围绕"减少 chunk 的物化次数"**。

`ExpressionExecutor` 把绑定表达式求值成一个 Vector。参考实现为了可读性走 `Value` 抽象；DuckDB 真实代码按类型特化成原始数组上的紧循环。

**Filter 的零拷贝技巧**：`PhysicalFilter` 不复制数据，而是产出一个 `SelectionVector`（匹配行的下标数组），`DataChunk::Slice` 用它做压缩。DuckDB 里 selection vector 还能原样传给下游，连压缩都省了。

## 2. push-based 流水线（L3.T3 的骨架）

经典火山模型是 **pull**：父算子调子算子的 `Next()`。push-based 反过来——**数据从源推向汇**：

```
一个 pipeline = 一个 source → 若干 operator → 至多一个 sink
```

物理算子的三种角色（一个算子可身兼数职）：

| 角色 | 接口 | 算子 |
|------|------|------|
| SOURCE | `GetData` 产 chunk | 表扫描；聚合/排序/LIMIT 在 Finalize 之后 |
| OPERATOR | `Execute` 就地变换 chunk | Filter、Projection、Join 的 probe 侧 |
| SINK | `Sink/Combine/Finalize` 吃 chunk | 聚合、排序、LIMIT、Join 的 build 侧、结果收集器 |

`PipelineBuilder` 按依赖把计划树切成多条 pipeline：

```
SELECT ... FROM orders JOIN customer ON ... GROUP BY ... 的物理计划

pipeline 1: scan(customer)            → sink: HashJoin(build)   ┐ 先跑
pipeline 2: scan(orders) → probe Join → sink: HashAggregate     ┘ 再跑
pipeline 3: source=HashAggregate      → sink: ResultCollector      最后跑
```

为什么必须切？因为 sink 是**流水线阻断点**（pipeline breaker）：build 侧没吃完，probe 一个键都匹配不了；聚合没 finalize，一行结果都没有。DuckDB 的 `PipelineEvent`/依赖图调度的就是这个，我们简化成"按序串行执行 pipeline"。

## 3. morsel-driven 并行（L3.T3）

每条 pipeline 内部，N 个线程共享一个 `ParallelTableScanState`，通过 Lab 0 的原子队列动态领取 morsel（行组内向量化大小的切片）：

- 扫描线程互不分配固定数据，快者多劳；
- 各算子的中间状态**线程本地化**：聚合的哈希表每线程一个，零锁；
- 线程领不到 morsel 后 `Combine`：把局部状态并进全局（此时才加锁，且只加一次）。

**zone map 下推**：`PhysicalPlanGenerator` 把 WHERE 里 `列 比较符 常量` 的合取项抽出来塞进 `PhysicalTableScan::table_filters`；扫每个 morsel 前先问行组的 zone map，整片不可能命中就跳过。注意 zone map 是近似判断，所以 `PhysicalFilter` 仍要保留——下推只是加速，正确性靠 filter 兜底。

## 4. 两个有趣的算子（L3.T4 - T6）

**HashJoin 一身三职**：build 侧是 sink（建哈希表），probe 侧是 operator。probe 有个真实系统都会遇到的坑：一个输入 chunk 可能产出**超过 2048 行**（一键多匹配），`Execute` 必须可续传——把未消费完的输入和匹配游标存进 `HashJoinProbeState`，下次调用接着发。另外别忘了 SQL 语义：NULL 连接键永不匹配。

**HashAggregate 的两阶段**：线程各自 `Sink`（向量化求值分组键 + 逐行折叠进局部哈希表）→ `Combine`（加锁合并）→ `Finalize`（物化结果行）→ 之后它自己变成 source 发结果。边界情况：无 GROUP BY 的聚合在空表上也要产出恰好一行（`count(*)=0, sum=NULL`）。

## 测试

```bash
./tdbtest   # Lab3ExecutionTest.* 共 23 个端到端 SQL 用例
```

特别地，`ParallelScanConsistency` / `ParallelGroupByConsistency` / `JoinLargeParallel` 会在 1 线程与 4 线程下跑同一查询并断言结果一致——并行不该改变语义。

## 思考题

- 为什么聚合要"局部表 + Combine"而不是一把全局锁保护一个哈希表？如果分组基数极小（比如只有 3 个组），哪种反而更快？
- `PhysicalLimit::Sink` 直接往全局状态写（加锁）而没有用线程局部状态，为什么这样做对 LIMIT 是合理的？
- 想再进一步：给 Join 加 build 侧选择（选较小的孩子建表）需要 Planner 知道什么信息？
