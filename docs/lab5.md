# Lab 5 - Vector Expression 与相似度检索

> 对应代码：`src/common/vector_operations.cpp`、`src/binder/binder.cpp`、`src/execution/expression_executor.cpp`
> 对应测试：`test/lab5_vector_expression_test.cpp`（8 个用例）
> 前置 Lab：Lab 1（列存）+ Lab 2（Binder）+ Lab 3 T1/T2/T6（表达式、Projection、OrderBy/Limit）

## Overview

本 Lab 给 tiny-duckdb 增加 **embedding 向量**的存储和检索能力。完成后，下面的 SQL 会执行一次真正的语义向量 Top-K 检索：

```sql
CREATE TABLE docs (
    id INTEGER,
    title VARCHAR,
    embedding VECTOR(3)             -- 3 维 embedding 列
);

INSERT INTO docs VALUES
    (1, 'database systems',  [1, 0, 0]),
    (2, 'query execution',   [0.9, 0.1, 0]),
    (3, 'cooking',           [0, 0, 1]);

SELECT id, title,
       cosine_distance(embedding, [1, 0, 0]) AS distance
FROM docs
ORDER BY distance
LIMIT 2;
-- 预期结果：database systems (dist=0), query execution (dist≈0.0061)
```

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| T1 | 三个距离 kernel（L2 / 余弦 / 负内积） | `./tdbtest Lab5VectorExpressionTest.DistanceKernels` |
| T2 | Binder 函数绑定：名称/参数数量/类型/维度检查 | `./tdbtest Lab5VectorExpressionTest.BinderChecksFunctionAndDimension` |
| T3 | ExpressionExecutor：批量（Vector-at-a-time）求距离 + NULL 传播 | `./tdbtest Lab5VectorExpressionTest.ExactTopK*` |

> 📌 **SQL 接线（不单独计分）**：如果你是在自己完成的 Lab 2 上继续，需要先在 PEG 语法、Transformer、Binder 中补全 VECTOR 类型和字面量支持（具体见下方"SQL 接线检查"）。如果你使用的是随参考解答提供的完整 Parser/Transformer，可以直接开始 T1-T3。

---

## Background

### 0. 系统全景——向量检索在 tiny-duckdb 中的位置

```
┌───────────────────────────────────────────────────────────────────────┐
│                         SQL 查询                                      │
│  SELECT id, title, cosine_distance(emb, [1,0,0]) AS d                │
│  FROM docs ORDER BY d LIMIT 2                                        │
└───────────────────────────────┬───────────────────────────────────────┘
                                │
                    ┌───────────▼───────────┐
                    │  Lab 2: Binder + Planner
                    │  - 识别 cosine_distance 函数         ← T2
                    │  - 验证两边都是 VECTOR(n)
                    │  - 生成物理计划：
                    │    OrderBy(limit)
                    │      └─ Projection(id, title, dist)
                    │           └─ TableScan(docs)
                    └───────────┬───────────┘
                                │
                    ┌───────────▼───────────┐
                    │  Lab 3: Push 执行引擎
                    │  Pipeline:
                    │  Scan(docs) → Projection → OrderBy(sink)
                    │                  │
                    │         cosine_distance(emb, const)  ← T3
                    │           ↓ 逐 chunk 2048 行批量计算
                    └───────────┬───────────┘
                                │
                    ┌───────────▼───────────┐
                    │  Lab 1: 列存
                    │  VECTOR(3) 列存在 ColumnChunk 中
                    │  每格是 std::vector<double>(3)
                    └───────────────────────┘

                    数学 kernel: L2/Cosine/NIP     ← T1
```

**跨 Lab 串联**：
- Lab 1 的列存 → VECTOR 类型是普通列类型，复用 ColumnChunk 存储（无需改存储层）；
- Lab 2 的 Binder → T2 在 Binder 中加一个函数绑定分支；
- Lab 3 的 ExpressionExecutor → T3 加 VECTOR_DISTANCE 表达式类型的求值分支；
- Lab 3 的 OrderBy + Limit → 零修改直接复用，作为 exact k-NN 的排序+截断。

### 1. 先搞清楚两个"Vector"——最容易混淆的概念

**本 Lab 有两个完全不同的东西都叫 vector**，一定要区分：

```
┌──────────────────────────────────────────────────────────────────┐
│  SQL 层的 VECTOR(n)：一个数据类型，一行中的一个值                  │
│                                                                  │
│    VECTOR(3) = [1.0, 0.0, 0.0]                                   │
│    这是一行的 embedding，3 个 double 组成的定长数组                │
│    存在 Value::vector_value_ 中（std::vector<double>）            │
│    对应 LogicalType::VECTOR(n)                                   │
├──────────────────────────────────────────────────────────────────┤
│  执行引擎的 Vector：一列 2048 行的批量数据                         │
│                                                                  │
│    ┌────────────────────────────────┐                             │
│    │ Vector (执行器概念)             │                             │
│    │  行 0: [1.0, 0.0, 0.0]  (VECTOR(3))                        │
│    │  行 1: [0.9, 0.1, 0.0]  (VECTOR(3))                        │
│    │  行 2: [0.0, 0.0, 1.0]  (VECTOR(3))                        │
│    │  ...                            │                             │
│    │  2048 行，每行是一个 VECTOR(3)  │                             │
│    └────────────────────────────────┘                             │
│    内部存储：vector_heap_ = vector<vector<double>>(2048)          │
└──────────────────────────────────────────────────────────────────┘
```

**记忆法**：
- SQL `VECTOR(n)` → 数据类型（像 INTEGER、DOUBLE 一样），一行一个值；
- 执行引擎 `Vector` → 数据容器（一列数据），一批 2048 个值。

T3 的计算是**两层循环**：外层遍历 2048 行（执行 Vector 的 batch），内层对每行的 VECTOR(n) 调距离 kernel（n 个 double 的运算）。

### 2. Embedding 是什么？

Embedding 模型（如 BERT、CLIP、Sentence-BERT）把文本/图像/商品映射成一个固定维度的实数向量：

```
文本 "database query optimizer"
    → embedding 模型
    → [0.12, -0.81, 0.44, 0.03, ...]  (n=768 个 double)
```

模型训练的目标是：**语义接近的对象在向量空间中也接近**。数据库不负责产生 embedding——那是 AI 模型的事；数据库负责：
1. 存储向量；
2. 计算查询向量与每行向量的距离；
3. 找出距离最近的 k 行（k-NN）。

```
向量空间示意（2D 投影，方便可视化）：
═════════════════════════════════

                    ○ "cooking recipe"
                   /
                  /
                 /  ● "bake bread"
                /
               /
    ──────────●──────────────────
              ●[1,0] "database"
             /  ○ "SQL optimizer"
            /  ● "query execution"
           /
          /  ○ "transaction processing"
         /
        ○ "machine learning"

查询向量 q = [1,0]（"database"）
最近邻: "database" (dist=0) → "query execution" (dist≈0.1)
     → "SQL optimizer" → "transaction processing" → ...
```

固定维度是类型的一部分：`VECTOR(3) ≠ VECTOR(4)`。这让 Binder 可以在执行前拒绝维度错误（比如把 3 维向量和 4 维向量做距离计算），而不是扫描了几百万行才崩溃。

### 3. 三种距离度量

设两向量 $x, y \in \mathbb{R}^d$。三种距离都是逐元素运算，结果是一个 double 值，**越小表示越相似**。

#### 3.1 L2 距离（欧氏距离）

$$d_{L2}(x, y) = \sqrt{\sum_{i=1}^{d}(x_i - y_i)^2}$$

- 几何意义：向量空间中的直线距离；
- 特点：受向量长度（magnitude）和方向共同影响；
- 取值范围：$[0, +\infty)$，0 = 完全相同。

```
L2 示意：
  x = [1, 0]  ●─── d=? ────●  y = [0.9, 0.1]
               \         /
                \       /
                 \     /
                  \   /
                   \ /
                    ● (0,0)
  d² = (1-0.9)² + (0-0.1)² = 0.01 + 0.01 = 0.02
  d  = √0.02 ≈ 0.141
```

SQL 写法：`l2_distance(embedding, [0.1, 0.2, 0.3])`
别名（DuckDB 兼容）：`array_distance(embedding, [0.1, 0.2, 0.3])`

#### 3.2 余弦距离（Cosine Distance）

$$d_{cos}(x, y) = 1 - \frac{x \cdot y}{\|x\|_2 \|y\|_2}$$

- 几何意义：1 - 余弦相似度，衡量的是**方向差异**而非长度差异；
- 特点：只关心角度，不关心向量长度。两个同方向向量距离为 0，正交为 1，反向为 2；
- 取值范围：$[0, 2]$；
- **零向量问题**：$x$ 或 $y$ 是零向量时分母为 0，本 Lab 抛出 `ExecutorException`。

```
余弦距离示意：
  x = [1,0]   ●─┐
                │ θ               y = [0.9, 0.1] 几乎同向
                │                 cos θ ≈ 0.9/√(0.81+0.01) ≈ 0.994
                ●(0,0)            d_cos = 1 - 0.994 ≈ 0.006

  x = [1,0]   ●─┐
                ├─── z = [0,1]  (正交，cos=0)
                │                d_cos = 1 - 0 = 1
                ●(0,0)──● z

  x = [1,0]   ●── w = [-1, 0]  (反向，cos=-1)
                \               d_cos = 1-(-1) = 2
                 \
                  ● w
```

实现提示：一个循环同时累加三个量：
- `dot = Σ x[i]*y[i]`
- `norm_x² = Σ x[i]²`
- `norm_y² = Σ y[i]²`

最后：`1.0 - dot / (sqrt(norm_x²) * sqrt(norm_y²))`

SQL 写法：`cosine_distance(embedding, [0.1, 0.2, 0.3])`
别名：`array_cosine_distance(embedding, [0.1, 0.2, 0.3])`

#### 3.3 负内积（Negative Inner Product）

$$d_{nip}(x, y) = -(x \cdot y) = -\sum_{i=1}^{d}x_i y_i$$

- 内积 $x·y$ 越大通常越相似，但 SQL 的 `ORDER BY distance ASC LIMIT k` 要求"小=近"，所以取负号；
- **注意不要把内积直接升序排列**，否则会选出最不相似的行！
- 与余弦距离的关系：如果向量已经 L2-归一化为单位长度（$\|x\|=\|y\|=1$），内积 = 余弦相似度，此时 NIP 排序等价于余弦距离排序。

```
负内积示意（不归一化）：
  x = [3, 0], y = [2, 0]:
    dot = 6 → d_nip = -6  (非常小 = 非常相似)
  x = [3, 0], y = [1, 0]:
    dot = 3 → d_nip = -3
  x = [3, 0], y = [0, 1]:
    dot = 0 → d_nip = 0
```

SQL 写法：`negative_inner_product(embedding, [0.1, 0.2, 0.3])`
别名：`array_negative_inner_product(embedding, [0.1, 0.2, 0.3])`

#### 归一化后的等价关系

如果所有向量都归一化为单位长度（$\|v\|=1$），三种距离有严格的数学关系：

$$\|x-y\|^2 = 2 - 2(x \cdot y)$$

此时 L2 距离排序、余弦距离排序、负内积排序**给出完全相同的 Top-K 顺序**。但未归一化时不能假设等价——L2 受长度影响，余弦只看方向。

### 4. `ORDER BY distance LIMIT k` 为什么就是 k-NN 检索？

查询向量 $q$ 是常量（SQL 字面量或参数）。表中每行有 embedding $v_i$。我们计算：

$$(row_i, d(v_i, q)),\quad i = 1, \ldots, N$$

然后按距离升序排列，取前 k 行——这就是 **exact k-nearest neighbors（精确 k-NN）**的定义。

在 tiny-duckdb 中，这条 SQL 的执行路径是：

```
执行计划（物理算子树）：
══════════════════════

Limit 2 (sink/source: 截断到 2 行)
  └─ OrderBy distance ASC (sink/source: 全物化+排序)
       └─ Projection(id, title, cosine_distance(emb, [1,0,0]) AS dist)
            └─ TableScan(docs, columns=[id, title, emb])
```

Pipeline 拆分（参考 Lab 3 第 4 节）：

```
Pipeline 1:
  source: TableScan(docs)
  operators: [Projection(id,title,dist)]
  sink: OrderBy (物化所有行，按 dist 排序)

Pipeline 2:
  source: OrderBy (Finalize 后，单线程按 offset 吐出)
  operators: []
  sink: Limit (截断到 2)

Pipeline 3:
  source: Limit
  sink: ResultCollector
```

```
Pipeline 1 并行执行示意（4 线程）：
═══════════════════════════════════

线程0: morsel(0-2047) → Scan → cosine_distance(2048行) → OrderBy.Sink(局部)
线程1: morsel(2048-4095) → Scan → cosine_distance(2048行) → OrderBy.Sink(局部)
线程2: morsel(4096-6143) → Scan → cosine_distance(2048行) → OrderBy.Sink(局部)
线程3: morsel(6144-8192) → Scan → cosine_distance(部分)   → OrderBy.Sink(局部)
                                                              ↓
                                         Combine 合并所有局部行到全局
                                                              ↓
                                         Finalize: 按 dist 全局排序
                                                              ↓
Pipeline 2+3: 单线程 drain 有序结果，Limit 截前 2 行返回
```

**复杂度分析**：
- 距离计算：$O(N \cdot d)$（N 行 × 维度 d）——必须算完所有行，无法跳过；
- 排序：$O(N \log N)$（OrderBy 全排序）；
- 内存：$O(N)$（物化所有行 + 距离）。

这是 exact k-NN 的暴力解法，是后续评价近似索引（ANN）**recall 的 ground truth**——任何近似算法的结果都要和这个 exact 结果比对。更高效的 exact Top-K 可以用大小为 k 的最大堆（$O(N \log k)$），但仍需计算全部 N 个距离。

### 5. 近似最近邻（ANN）与 HNSW（背景知识，不实现）

真实向量数据库不会每次都扫全表。DuckDB 的 VSS 扩展支持 **HNSW（Hierarchical Navigable Small World）** 索引：

```
HNSW 多层近邻图示意：
══════════════════════

Layer 2:  ○──────────○        （入口层，点少、边长，快速粗定位）
           \        /
            \      /
Layer 1:    ○────○────○      （中间层）
            /|   |    \
           / |   |     \
Layer 0: ○──○──○○──○──○──○   （底层，所有点都在，精细搜索）
          |  | /\   |  |
          ○──○──○───○──○      (近邻连接)

搜索：从入口层贪心往下找，每层缩小范围，底层精细搜索。
只访问 ~log(N) 个点，而非全部 N 个。
```

HNSW 的 trade-off：
- ✅ 快：只访问少量候选点；
- ❌ 不保证精确：可能漏掉真正的最近邻（approximate NN）；
- 参数 `ef_search` 越大 → 访问候选越多 → recall 越高、延迟越高；
- 建索引需要额外时间和内存。

本 Lab 不实现 HNSW。先有可验证的 exact 路径，才能定义 ANN 的正确性：

$$\text{recall@}k = \frac{|\text{ANN top-}k \cap \text{exact top-}k|}{k}$$

---

## SQL 接线检查（不单独计分，仅在自建 Lab 2 时需要）

如果你的 Lab 2 是自己写的（而非用 reference 版的 Parser/Transformer/Binder），需要补全以下 VECTOR 支持：

1. **PEG 语法**（Lab 2 parser）：
   - `TypeName` 规则最前面增加 `VectorType <- 'vector' '(' ArraySize ')'`，其中 `ArraySize <- [1-9] [0-9]*`（正整数，不允许前导 0，也不允许 0 维）；
   - `Literal` 规则最前面增加 `VectorLiteral <- '[' VectorElement (',' VectorElement)* ']'`，其中 `VectorElement <- '-'? Number`（每个元素可选负号）；
   - 单引号字面量大小写不敏感，写 `'vector'` 即可同时匹配 `VECTOR`/`Vector`。
2. **Transformer**：
   - `TransformCreateTable` 将 `VECTOR(n)` 转为 `LogicalType::Vector(n)`；
   - `TransformLiteral` 将 `[a, b, c]` 转为 `Value::Vector({a, b, c})`。
3. **Binder（INSERT）**：
   - `BindInsert` 的 VECTOR 分支要求 literal 类型与目标列类型完全相同，维度必须一致，不允许隐式截断或补齐。

验证：
```bash
./tdbtest Lab5VectorExpressionTest.VectorTypeAndLiteralParsing
./tdbtest Lab5VectorExpressionTest.InsertRejectsWrongDimension
```

如果使用的是 answer 分支完整代码，这些已经接好，直接进入 T1。

---

## Task #1 - Distance Kernels

**文件**：`src/common/vector_operations.cpp`

实现 `VectorOperations` 的三个静态方法：

```cpp
static double L2Distance(const std::vector<double>& left,
                         const std::vector<double>& right);
static double CosineDistance(const std::vector<double>& left,
                             const std::vector<double>& right);
static double NegativeInnerProduct(const std::vector<double>& left,
                                   const std::vector<double>& right);
```

`ValidateDimensions` 已提供（检查非空和维度一致），在每个函数开头调用即可。

**实现提示**：
- **L2Distance**：累加 `(left[i]-right[i])²`，循环结束后调用一次 `sqrt`；
- **CosineDistance**：同一个循环累加 `dot`、`norm_l²`、`norm_r²`，最后 `1.0 - dot / (sqrt(norm_l²)*sqrt(norm_r²))`；
  - ⚠️ 不要返回 cosine similarity（直接是 `dot/(||l||·||r||)`）——必须是 `1 - similarity`；
  - 零向量（norm 为 0）是非法输入，ValidateDimensions 之外可以不用额外处理（教学简化）；
- **NegativeInnerProduct**：累加 `left[i]*right[i]`，最后**取负**；
  - ⚠️ 最后一步负号至关重要：没有负号就会把内积升序排，选出最不相似的行；
- 所有中间累加量用 `double`，不要用 `float` 或 `int`。

**手动验证**：
```
L2: [1,0,0] vs [0.9,0.1,0]
    = sqrt(0.1² + (-0.1)² + 0) = sqrt(0.02) ≈ 0.1414

Cosine: [1,0,0] vs [0.9,0.1,0]
    dot = 0.9, ||l||=1, ||r||=sqrt(0.82)≈0.9055
    similarity = 0.9/0.9055 ≈ 0.9939
    distance = 1 - 0.9939 ≈ 0.0061

NIP: [1,0,0] vs [0.9,0.1,0]
    dot = 0.9 → d = -0.9
```

**测试**：`DistanceKernels`（三种距离的精确值验证）。

---

## Task #2 - Binder 函数绑定

**文件**：`src/binder/binder.cpp`

实现 `BindVectorDistance` 并在 `BindExpression` 中分派。

### T2a `BindVectorDistance(function_expr, scope)`

Binder 的职责是**在执行前拒绝非法查询**，不让错误跑到计算层。步骤：

1. **函数名校验**：名称必须是以下 6 个之一：
   - 短名称：`l2_distance`、`cosine_distance`、`negative_inner_product`
   - DuckDB 别名：`array_distance`、`array_cosine_distance`、`array_negative_inner_product`
2. **参数数量**：恰好 2 个参数，不接受 `*` 或其他数量；
3. **递归绑定**：对左右两个 child 表达式分别调 `BindExpression(child, scope)`；
4. **类型校验**：两边的 return_type 都必须是 VECTOR 类型（`type.Id() == LogicalTypeId::VECTOR`）；
5. **维度校验**：两边的 LogicalType 必须完全相等（`left_type == right_type`），`LogicalType::operator==` 已经比较了 VECTOR 维度；
6. **映射距离类型**：根据函数名映射到 `VectorDistanceType::{L2, COSINE, NEGATIVE_INNER_PRODUCT}`；
7. **构造 BoundExpression**：返回 `BoundVectorDistanceExpression`，return_type 固定为 `LogicalType::Double()`。

### T2b 在 `BindExpression` 中分派

在处理函数调用的 AST 节点时（AST 中函数名节点的 type 是 `peg::Ast` 节点名），检查函数名是否是上述 6 个距离函数之一：
- 如果是 → 调 `BindVectorDistance`；
- 如果是聚合函数（count/sum/avg/min/max）→ 走原有聚合绑定路径；
- 其他 → 报错（与原有行为一致）。

**报错信息建议**：包含实际类型信息，比如：
```
"cosine_distance requires two VECTOR arguments, got VECTOR(3) and VECTOR(4)"
```
这能让学生在调试 `[1,2]` vs `VECTOR(3)` 错误时快速定位。

**测试**：
- `BinderChecksFunctionAndDimension`：合法绑定成功 + 非法（类型错、维度错、参数数错）都被 Binder 拒绝；
- `InsertRejectsWrongDimension`：INSERT 维度不匹配报错（INSERT 路径校验）。

---

## Task #3 - Vector-at-a-Time 执行

**文件**：`src/execution/expression_executor.cpp`

在 ExpressionExecutor 中增加 VECTOR_DISTANCE 表达式的求值，保持"一批表达式递归求值、批量写入结果"的风格。

### 实现 `EvaluateVectorDistance(expr, chunk, result)`

1. 对 `expr.left` 和 `expr.right` **各递归调用一次 Evaluate**，求值到临时 Vector（或直接用 child Vector）；
2. 结果 Vector 是 DOUBLE 类型，大小 = chunk.size()；
3. **逐行遍历**（外层循环：`i` 从 0 到 `chunk.size()-1`）：
   - 取出左 child 第 i 行的值（`left_val.GetVector()`）；
   - 取出右 child 第 i 行的值（`right_val.GetVector()`）；
   - **NULL 传播**：如果任一 child 第 i 行为 NULL，结果第 i 行也设为 DOUBLE NULL；
   - 非 NULL 时，根据 `expr.distance_type` 调用 T1 的对应 kernel；
   - 把 double 结果写入 result Vector 的第 i 行。
4. 在 `ExpressionExecutor::Evaluate` 的 switch 中增加 `case ExpressionType::VECTOR_DISTANCE:` 分支调用此 helper。

### 关键设计原则

**不要**为每行重新 Evaluate 整个子表达式——这违反向量化原则。正确做法是：
- 左 child Evaluate **一次**，得到所有 2048 行的左向量；
- 右 child Evaluate **一次**，得到所有 2048 行的右向量；
- 然后逐行调 kernel。

```
正确（Vector-at-a-time）：
═════════════════════════
  Evaluate(left_child)  → left_vec  (2048 个 VECTOR 值)
  Evaluate(right_child) → right_vec (2048 个 VECTOR 值)
  for i in 0..2047:
    if left_vec[i] is NULL or right_vec[i] is NULL:
      result[i] = NULL
    else:
      result[i] = kernel(left_vec[i], right_vec[i])

错误（Row-at-a-time，反模式）：
═══════════════════════════════
  for i in 0..2047:
    Evaluate(left_child on just row i)   ← 每行递归整个表达式树！
    Evaluate(right_child on just row i)
    result[i] = kernel(...)
```

### NULL 语义

- 距离函数任一输入为 NULL → 结果为 NULL（DOUBLE 类型的 NULL）；
- NULL 传播由 ExpressionExecutor 统一处理，kernel 函数不需要处理 NULL；
- 当前 tiny-duckdb 的 ORDER BY 把 NULL 排在最前面，所以 embedding 列应保持非 NULL（教学引擎的已知简化）。

**测试**：
- `ExactTopKCosineQuery`：端到端 cosine distance Top-K；
- `L2AndInnerProductQueries`：L2 和 NIP 也工作；
- `NullDistancePropagates`：NULL embedding 产生 NULL distance；
- `ExactTopKAcrossExecutionChunks`：多行数据跨 2048 行 chunk 边界仍正确（验证多 morsel/多chunk 下的正确性）。

---

## Testing

```bash
make clean && make -j4     # 改了头文件后要 clean（Makefile 不追踪头依赖）
./tdbtest Lab5VectorExpressionTest.DistanceKernels
./tdbtest Lab5VectorExpressionTest.BinderChecksFunctionAndDimension
./tdbtest Lab5VectorExpressionTest.ExactTopKCosineQuery
./tdbtest Lab5VectorExpressionTest.L2AndInnerProductQueries
./tdbtest Lab5VectorExpressionTest.NullDistancePropagates
./tdbtest Lab5VectorExpressionTest.ExactTopKAcrossExecutionChunks
./tdbtest Lab5             # 跑全部 8 个用例
```

**手工验收**：
```bash
./tiny_duckdb_shell
# 粘贴 Overview 里的 CREATE + INSERT + SELECT SQL
```

### 里程碑自检

| 完成 | 可用功能 | 验证 |
|------|---------|------|
| T1 | 纯数学：三个距离 kernel | DistanceKernels |
| T1-T2 | Binder 能识别/拒绝距离函数 | BinderChecksFunctionAndDimension |
| T1-T3 | 端到端 k-NN 查询 | ExactTopK*, NullDistancePropagates |

---

## Development Hints

1. **先单线程验证**：`./tiny_duckdb_shell` 里 `.threads 1` 先调试单线程正确性，再开 4 线程验证跨 chunk 一致性。
2. **Binder 先行**：T2 完成后，错误查询应该在 bind 阶段就报错。如果错误查询到了执行阶段才崩，说明 Binder 校验不够严格。
3. **Kernel 精度**：测试容忍少量浮点误差（`EXPECT_NEAR`），但要注意 `1.0 - similarity` 和直接返回 similarity 会有方向性错误——手工算一组值对照。
4. **Cosine 零向量**：测试不刻意构造零向量输入，所以 kernel 里不需要做额外除零保护（ValidateDimensions 只检查维度和非空）。
5. **结果 Vector 类型**：距离结果永远是 DOUBLE（LogicalType::Double()），不管输入 VECTOR 的元素是啥。
6. **多看 ExpressionExecutor 现有代码**：T3 里处理 BoundComparison、BoundArithmetic 的模式就是你处理 VECTOR_DISTANCE 的模板——Evaluate child → 逐行合并 → 写结果。

---

## Grading Rubric

1. `./tdbtest Lab5` 全部 8 个用例通过（含 4 线程跨 execution chunk 的 Top-K）；
2. Lab 0-4 的所有回归测试仍然通过（不破坏已有功能）；
3. 三种距离函数不硬编码维度或测试数据（对任意 d 维 VECTOR 都正确）；
4. 维度/类型/参数数量错误由 Binder 拒绝（不在执行期才崩溃）；
5. ExpressionExecutor 按 chunk 递归 Evaluate child（不做逐行子表达式重算）；
6. NULL 输入产生 NULL 输出；
7. 编译零警告。

---

## 进阶练习（不计分）

1. **Top-K 堆优化**：把 OrderBy + Limit 融合成一个算子，维护大小为 k 的最大堆。距离计算后直接与堆顶比较，无需物化所有行再排序。对比 N=10⁶, k=10 时的时间和内存（O(N log k) vs O(N log N)，内存 O(k+d) vs O(N)）。
2. **度量一致性验证**：对同一批归一化向量分别跑三种 metric，验证 Top-K 结果一致（用公式 $\|x-y\|^2=2-2(x·y)$ 解释）；对未归一化向量，观察结果差异。
3. **两阶段检索 + 精排**：先生成近似候选集（比如随机采样或简单的倒排），再用本 Lab 的 exact distance 做 rerank。为什么许多向量数据库采用"召回+精排"两阶段架构？
4. **SIMD 优化 kernel**：当前 embedding 存在 `vector<vector<double>>`（每行指针跳转）。改成 N×d 的连续内存（`vector<double>` of size N*d），用 SIMD（AVX2/AVX-512）一次算 4/8 个 double 的距离。
5. **MinHash/HNSW 简化版**：设计一个单层近邻图（greedy search），统计 recall@k 与访问点数。HNSW 比它多了什么结构（多层、entry point 选择）？

---

## 思考题（不计分）

- 为什么 `ORDER BY distance LIMIT k` 在列存引擎里能天然并行（morsel 并行），而 B+ 树索引做 k-NN 却很难并行？
- 我们的实现中，cosine_distance 对零向量会除零崩溃。真实系统应该怎么处理？（提示：零向量的 embedding 语义是什么？）
- 如果向量维度 d=1536（OpenAI text-embedding-ada-002 的维度），N=10 亿行，暴力扫描需要多少次 double 乘法？DDR4 内存带宽 ~50GB/s，理论下限是多少秒？HNSW 为什么快？
- Lab 3 里 Zone map 可以在 TableScan 阶段跳过整 morsel。对向量距离计算，有没有类似的"整块跳过"优化？（提示：SIMD 快速边界、multi-pivot 剪枝、IVF 聚类。）
- 为什么三种距离都是"越小越近"但范围不同（L2 ∈ [0,∞), cosine ∈ [0,2], NIP ∈ (-∞,∞)）？这对 Top-K 堆优化里的初始哨兵值有什么影响？
