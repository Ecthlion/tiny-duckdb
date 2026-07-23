# Lab 5 - Vector Expression 与相似度检索

> 对应代码：`src/common/vector_operations.cpp`、`src/binder/binder.cpp`、`src/execution/expression_executor.cpp`  
> 对应测试：`test/lab5_vector_expression_test.cpp`（7 个用例）  
> 前置 Lab：Lab 1（列存）+ Lab 2（Binder）+ Lab 3 T1/T2/T6（表达式、Projection、Order/Limit）

## Overview

本实验给 tiny-duckdb 增加 embedding 的计算能力。完成后，下面的 SQL 会执行一次真正的语义向量 Top‑K 检索：

```sql
CREATE TABLE docs (
    id INTEGER,
    title VARCHAR,
    embedding VECTOR(3)
);

INSERT INTO docs VALUES
    (1, 'database systems', [1, 0, 0]),
    (2, 'query execution',  [0.9, 0.1, 0]),
    (3, 'cooking',          [0, 0, 1]);

SELECT id, title,
       cosine_distance(embedding, [1, 0, 0]) AS distance
FROM docs
ORDER BY distance
LIMIT 2;
```

预期最近的两行是 `database systems`、`query execution`。

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| Task L5.T1 | 实现 L2、余弦距离、负内积三个数学 kernel | `./tdbtest Lab5VectorExpressionTest.DistanceKernels` |
| Task L5.T2 | Binder：函数名、参数数量、类型和维度检查 | `./tdbtest Lab5VectorExpressionTest.BinderChecksFunctionAndDimension` |
| Task L5.T3 | ExpressionExecutor：按 DataChunk 批量求距离、传播 NULL | `./tdbtest Lab5VectorExpressionTest.ExactTopKCosineQuery` |

`LogicalType::Vector(n)`、`Value::Vector(...)` 及其列式存储已经提供。参考解答版也已经接好 SQL 语法；若你使用 `strip_solutions.sh` 生成学生版并自行完成过 Lab 2，请先按下面的“SQL 接线检查”把自己写的 Parser/Transformer 扩展到 VECTOR，再开始三个评分任务。这样评分重点仍放在“一个新标量表达式如何穿过 Binder 与向量化执行器”，而不是重复 Lab 1/2 的大段样板代码。

### SQL 接线检查（不单独计分）

这是在你自己的 Lab 2 解答上做的小扩展：

1. `TypeName` 增加 `VectorType <- 'vector' '(' ArraySize ')'`，其中维度必须为正整数；
2. `Literal` 最前面增加 `VectorLiteral <- '[' VectorElement (',' VectorElement)* ']'`；
3. `TransformCreateTable` 把 `VECTOR(n)` 变成 `LogicalType::Vector(n)`；
4. `TransformLiteral` 把每个带可选负号的 Number 收进 `Value::Vector`；
5. `BindInsert` 的 VECTOR 分支要求 literal 类型与目标类型完全相同，维度不允许隐式补齐或截断。

用下面的测试确认接线完成：

```bash
./tdbtest Lab5VectorExpressionTest.VectorTypeAndLiteralParsing
./tdbtest Lab5VectorExpressionTest.InsertRejectsWrongDimension
```

## Background

### 1. embedding 是什么？

embedding 模型把文本、图像或商品映射为固定维度实数数组：

```text
"database query optimizer" → [0.12, -0.81, 0.44, ...]
```

模型训练的目标使语义接近的对象在向量空间中也接近。数据库不负责产生 embedding；它负责存储向量、计算距离，并找到离查询向量最近的行。

固定维度是类型的一部分：

```text
VECTOR(3) != VECTOR(4)
```

这能让 Binder 在执行前拒绝维度错误，而不是扫描几百万行后才失败。真实 DuckDB 使用固定长度 `ARRAY`，典型列类型是 `FLOAT[768]`；tiny-duckdb 用更直白的 `VECTOR(768)`，元素统一存为 DOUBLE。

### 2. 三种距离

设两向量 \(x,y\in\mathbb{R}^d\)。

#### L2（欧氏距离）

\[
d_{L2}(x,y)=\sqrt{\sum_{i=1}^{d}(x_i-y_i)^2}
\]

它衡量空间中的直线距离，越小越相似。量纲和向量长度都会影响结果。

SQL：

```sql
l2_distance(embedding, [0.1, 0.2, 0.3])
```

也支持 DuckDB 同名写法 `array_distance(...)`。

#### Cosine distance（余弦距离）

\[
d_{cos}(x,y)=1-\frac{x\cdot y}{\|x\|_2\|y\|_2}
\]

它主要比较方向，弱化向量长度。相同方向距离为 0，正交为 1，反向为 2；任一向量为零时分母为零，本实验抛出 `ExecutorException`。

SQL：

```sql
cosine_distance(embedding, [0.1, 0.2, 0.3])
```

也支持 `array_cosine_distance(...)`。

#### Negative inner product（负内积）

\[
d_{ip}(x,y)=-(x\cdot y)=-\sum_{i=1}^{d}x_i y_i
\]

内积越大通常表示越相似，但 SQL 的 Top‑K 统一按“值越小越近”排序，所以取负号。不要把 `dot product` 原值升序排，否则会选出最不相似的行。

SQL：

```sql
negative_inner_product(embedding, [0.1, 0.2, 0.3])
```

也支持 `array_negative_inner_product(...)`。三个短名称便于课堂书写，三个 `array_*` 别名便于把 SQL 搬到真实 DuckDB 对照。

若模型已经把向量归一化为单位长度，余弦距离、L2 与内积排序有紧密关系：

\[
\|x-y\|_2^2=2-2(x\cdot y)
\]

这时它们往往给出相同的邻居顺序；未归一化时不能假设等价。

### 3. `ORDER BY distance LIMIT k` 为什么就是检索？

查询向量 \(q\) 是常量。表中每一行有 embedding \(v_i\)。数据库计算：

```text
(row_i, distance(v_i, q)), i = 1..N
```

然后按 distance 升序，取前 k 行。这正是 exact k-nearest neighbors（精确 k-NN）的定义。

在 tiny-duckdb 中，执行路径是：

```text
TableScan
   │ 每批最多 2048 行
   ▼
Projection
   ├─ 复制 id/title
   └─ cosine_distance(embedding, query)  ← Lab 5
   ▼
OrderBy（物化并对 N 行完整排序）
   ▼
Limit k
   ▼
ResultCollector
```

距离阶段对 N 行、每行 d 个元素做计算，时间为 \(O(Nd)\)。当前 `PhysicalOrderBy` 再做完整排序，约为 \(O(N\log N)\)，内存为 \(O(N)\)。这一定返回正确的最近邻，是以后评价近似索引 recall 的 ground truth。

一个更好的 exact Top‑K 算子可以维护大小为 k 的最大堆，把排序降为 \(O(N\log k)\)，但仍必须计算全部 N 个距离，无法解决超大表的扫描成本。

### 4. DuckDB 与 HNSW 做了什么？

DuckDB 核心提供固定长度 `ARRAY` 及 `array_distance`、`array_cosine_distance`、`array_negative_inner_product`。它的 VSS 扩展可以在 FLOAT ARRAY 列上建立 HNSW 索引，并识别“距离排序 + LIMIT”形状，把全表计算改写为 HNSW index scan。

HNSW（Hierarchical Navigable Small World）可理解为多层近邻图：

1. 每个向量是图上的点，并连接若干附近点；
2. 上层点少、边跨度大，用于快速靠近目标区域；
3. 从上到下做贪心搜索，底层在候选邻域内精细搜索；
4. 只访问少量候选，不再计算所有 N 行。

代价是它通常属于 approximate nearest neighbor（ANN）：

- 更快，但可能漏掉真正的第 k 个邻居；
- `ef_search` 越大，访问候选越多，recall 通常越高、延迟也越高；
- 建索引需要额外时间与内存，更新维护比纯列存复杂。

本 Lab 不实现 HNSW。先拥有可验证的 exact 路径，才能定义 ANN 的正确性基线：

\[
recall@k=\frac{|\text{ANN top-k}\cap\text{exact top-k}|}{k}
\]

参考 DuckDB 官方资料：

- [Array Type](https://duckdb.org/docs/current/sql/data_types/array)
- [Array Functions](https://duckdb.org/docs/current/sql/functions/array)
- [Vector Similarity Search Extension](https://duckdb.org/docs/current/core_extensions/vss)
- [Vector Similarity Search in DuckDB](https://duckdb.org/2024/05/03/vector-similarity-search-vss)

## Task #1 - Distance Kernels

实现 `VectorOperations` 的三个函数：

```cpp
static double L2Distance(const std::vector<double> &left,
                         const std::vector<double> &right);
static double CosineDistance(const std::vector<double> &left,
                             const std::vector<double> &right);
static double NegativeInnerProduct(const std::vector<double> &left,
                                   const std::vector<double> &right);
```

`ValidateDimensions` 已提供，会拒绝空向量和维度不一致。

Hints：

- L2 累加平方差，最后只调用一次 `sqrt`；
- cosine 在同一个循环中累加 `dot`、`left_norm_squared`、`right_norm_squared`；
- 不要把 cosine similarity 当 distance：结果需要 `1 - similarity`；
- negative inner product 最后的负号决定了升序 Top‑K 是否正确；
- 中间量使用 `double`，不要用整数或 `float`。

## Task #2 - Binder

实现：

```cpp
Binder::BindVectorDistance(FunctionExpression &function,
                           const BindScope &scope)
```

Binder 的职责是让非法查询在执行前失败：

1. 函数名必须是三个短名称之一，或对应的 DuckDB `array_*` 别名；
2. 恰好两个参数，不接受 `*`；
3. 递归绑定两个 child；
4. 两边都必须是 VECTOR；
5. 两边 `LogicalType` 必须相同，因此维度也相同；
6. 映射到对应的 `VectorDistanceType`；
7. 返回 `BoundVectorDistanceExpression`，返回类型固定为 DOUBLE。

最后还要在 `BindExpression` 遇到函数 AST（当前用 `AGGREGATE_COUNT` 作为未绑定函数的占位 type）时，将三个距离函数分派到 `BindVectorDistance`；聚合函数仍走原路径，未知函数仍应报错。

Hints：

- 不要在 Binder 计算距离，它只检查并构造表达式树；
- `LogicalType::operator==` 已比较 VECTOR 维度；
- 报错信息写出两个实际类型，调试 `[1, 2]` 对 `VECTOR(3)` 会容易很多；
- 参数可以是列或常量，所以必须调用 `BindExpression`，不能强转成列引用。

## Task #3 - Vector-at-a-time Execution

实现 `EvaluateVectorDistance`：

1. 按两个 child 的返回类型创建 scratch `Vector`；
2. 对两个 child 各递归 `Evaluate` 一次；
3. 遍历 `chunk.size()` 行；
4. 任一输入为 NULL，输出类型化的 DOUBLE NULL；
5. 否则按 `distance_type` 调用 Task 1 的 kernel；
6. 将 DOUBLE 距离写入结果 Vector。

并在 `ExpressionExecutor::Evaluate` 的 switch 中增加 `VECTOR_DISTANCE` 分支调用这个 helper。不要把 Lab 5 的循环直接塞进 Projection：表达式执行器才是所有 SELECT/WHERE 表达式共享的语义层。

注意“向量表达式”有两个不同的 vector：

- SQL `VECTOR(768)`：一行中的 embedding；
- 执行引擎 `Vector`：一列最多 2048 行组成的批。

本任务计算的是“一批 embedding 的距离”：外层循环走 2048 行，内层 kernel 走 embedding 的 d 个元素。总工作量仍为 \(N\times d\)，但算子调度和表达式递归是一批一次，而不是每行一次。

NULL 会传播为距离 NULL。tiny-duckdb 当前升序排序把 NULL 放在前面，又没有 `IS NOT NULL` 语法，因此用于检索的 embedding 列应保持非 NULL；这是教学引擎的已知限制。

## Testing

```bash
make -j4
./tdbtest Lab5VectorExpressionTest.DistanceKernels
./tdbtest Lab5VectorExpressionTest.Binder
./tdbtest Lab5VectorExpressionTest.ExactTopK
./tdbtest Lab5
```

手工验收可直接复制 Overview 的 SQL 到：

```bash
./tiny_duckdb_shell
```

## Grading Rubric

1. Lab 5 的 7 个测试全部通过；
2. 全部旧 Lab 回归测试通过；
3. 三种函数没有硬编码维度或测试数据；
4. 维度/类型错误必须由 Binder 拒绝；
5. 执行器按 chunk 递归求 child，不能为每行重新求整个表达式；
6. 编译无警告。

## 思考题与进阶练习

1. 把 `PhysicalOrderBy + PhysicalLimit` 融合成大小为 k 的堆，比较 \(N=10^6,k=10\) 时的时间和内存。
2. 对同一批归一化向量分别跑三种 metric，它们的 Top‑K 是否一致？用公式解释。
3. 生成一个近似候选集合，再用本 Lab 的 exact distance rerank。为什么许多向量数据库采用“两阶段召回 + 精排”？
4. 设计一个极简的 single-layer proximity graph，统计 `recall@k` 与访问点数。它与 HNSW 缺少的关键结构是什么？
5. 当前 embedding 在每个执行 `Vector` 中是 `std::vector<double>` 的数组。若改成连续的 `N × d` 内存，如何用 SIMD 优化距离 kernel？
