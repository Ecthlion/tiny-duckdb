# Lab 1 - 列式存储：行组、列块与 Zone Map

> 对应代码：[column_chunk.cpp](file:///Users/bytedance/Projects/tiny-duckdb/src/storage/column_chunk.cpp)、[row_group.cpp](file:///Users/bytedance/Projects/tiny-duckdb/src/storage/row_group.cpp)、[table_data.cpp](file:///Users/bytedance/Projects/tiny-duckdb/src/storage/table_data.cpp)
> 头文件在 [storage/](file:///Users/bytedance/Projects/tiny-duckdb/src/include/tiny_duckdb/storage/)，**任务规格写在头文件注释里**
> 对应测试：[lab1_storage_test.cpp](file:///Users/bytedance/Projects/tiny-duckdb/test/lab1_storage_test.cpp)（17 个用例）
> 卡住时按任务查看：[Lab 1 渐进式 Hints](hints/lab1-hints.md)

## Overview

这个 Lab 实现 tiny-duckdb 的存储层。BusTub 用的是**行存堆表**（TableHeap，适合 OLTP 点查），而 DuckDB 作为分析型引擎用的是**列式存储 + 行组水平分区 + zone map 跳读**——这就是分析数据库区别于事务数据库的根本设计。

你在 Lab0 写的 MorselQueue 是调度器，但调度器要有用，必须有东西可以调度——那就是存储层里的数据块。本 Lab 的最终产物（TableData + RowGroup + ColumnChunk）就是 Lab3 并行扫描的 morsel 来源。

```
┌──────────────────────────────────────────────────────────────────────┐
│                       tiny-duckdb 存储架构                           │
│                                                                      │
│  TableData（一张表）                                                 │
│  │                                                                   │
│  ├── RowGroup 0  ← 最多 ROW_GROUP_SIZE=8192 行  ← 并行调度分区       │
│  │   ├── ColumnChunk 0 (col "id", INTEGER)                          │
│  │   │   └── blocks_: [Vector(2048)] [Vector(2048)] [Vector(2048)]  │
│  │   │   └── zone map: min_, max_                                    │
│  │   ├── ColumnChunk 1 (col "name", VARCHAR)                        │
│  │   │   └── blocks_: [Vector(2048)] [Vector(2048)] [Vector(2048)]  │
│  │   │   └── zone map: min_, max_                                    │
│  │   └── ColumnChunk 2 ...                                          │
│  │                                                                   │
│  ├── RowGroup 1  ← 最多 8192 行                                     │
│  │   └── (同上结构)                                                  │
│  └── ...                                                            │
│                                                                      │
│  常量关系：                                                          │
│    STANDARD_VECTOR_SIZE = 2048  (types.hpp:15)  ← Vector 容量        │
│    ROW_GROUP_SIZE = 4 × 2048 = 8192  (types.hpp:17)  ← 行组大小     │
│    一个 morsel = RowGroup 内 ≤2048 行 = 一个 Vector 的数据量         │
└──────────────────────────────────────────────────────────────────────┘
```

两个常量刻意比 DuckDB 小两个数量级（DuckDB 行组 122880 行、向量 2048）：让你用几千行数据就能观察到多行组、多 morsel 的行为，不需要真的插入 100 万行。

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| Task L1.T1 | `ColumnChunk::Append`（跨块追加一列数据） | `./tdbtest Lab1StorageTest.ColumnChunk*` |
| Task L1.T2 | `ColumnChunk::Scan`（跨块读取一列数据） | 同上（与 T1 一体两面） |
| Task L1.T3 | zone map：维护 min/max + `CheckZoneMap` 裁剪判断 | `./tdbtest Lab1StorageTest.ZoneMap*` |
| Task L1.T4 | `RowGroup::Append` / `RowGroup::Scan`（按列路由 + 投影下推） | `./tdbtest Lab1StorageTest.RowGroup*` |
| Task L1.T5 | `TableData::Append`（行组切分 + 并发安全） | `./tdbtest Lab1StorageTest` 全过 |

任务依赖关系：**T1/T2 → T4 → T5**（逐层组装），T3（zone map）可在 T1 完成后独立插入。建议严格按顺序实现。

## Background

### 为什么是列存？

行存（BusTub 的 TableHeap 或 MySQL 的 InnoDB 页）把一行的所有列连续存放：

```
行存布局（每行连续）：
  [id=1, name="Alice", score=95] [id=2, name="Bob", score=87] [id=3, ...] ...
   ←────────── 一行 ──────────→  ←────────── 一行 ──────────→

  查询 SELECT AVG(score) FROM t：
  → 必须读全部三列（因为行连续），即使只需要 score
  → 缓存行里塞满了 name 字符串，score 的缓存命中率低
```

列存把**每一列单独连续存放**：

```
列存布局（每列连续）：
  ColumnChunk "id":    [1] [2] [3] [4] ... [N]      ← 纯整数连续数组
  ColumnChunk "name":  ["Alice"] ["Bob"] ["Carol"]  ← 纯字符串
  ColumnChunk "score": [95] [87] [92] [78] ...      ← 纯整数连续数组

  查询 SELECT AVG(score) FROM t：
  → 只读 score 那一列，id 和 name 一个字节都不碰！
  → score 是连续整数数组，CPU 预取完美命中，SIMD 友好
```

分析型查询的典型形态是"扫 1 亿行，只用其中 2-3 列"，列存的 I/O 节省和缓存优势是压倒性的。代价是整行点查昂贵（要拼 N 个列段）和写入要写 N 个列——分析场景不在意这些。

### ColumnChunk 内部：Vector 块列表

一个 ColumnChunk 是"一个行组的一列"，但内部也不是一个巨大数组，而是分成 `blocks_`——每个 block 是一个 Vector，最多装 2048（STANDARD_VECTOR_SIZE）个值：

```
ColumnChunk（一列，最多 8192 行）：
  blocks_[0]: Vector ─── 2048 个值 ───┐
  blocks_[1]: Vector ─── 2048 个值    │  这些 Vector 在 Scan 时
  blocks_[2]: Vector ─── 2048 个值    │  被 Lab3 的 morsel 直接引用
  blocks_[3]: Vector ─── 2048 个值 ───┘
               ↑
            count_ 跟踪实际填充了多少个值
```

这种设计让 Append 和 Scan 都可以按 Vector 边界来切分：一次 Append 最多写满一个 block，一次 Scan 最多读出一个 Vector——而 Vector 正是 Lab3 执行引擎的基本处理单位（DataChunk 里的每列就是一个 Vector）。

```
Append(data, source_offset=2000, count=200) 的跨块场景：

  blocks_: [ 已填满 2048 │ 空闲... ]
                    ↑
             最后一个 block 还剩 2048-2000=48 个位置

  第 1 步：拷贝 48 个值到 blocks_[-1]（填满）
  第 2 步：新建 block
  第 3 步：拷贝剩余 200-48=152 个值到新 block
```

### zone map（区域地图）——不读数据就知道"这组没有匹配行"

这是分析引擎最朴素也最有效的优化。每个 ColumnChunk 顺手记录本块所有非 NULL 值的 `min_` 和 `max_`：

```
查询：SELECT COUNT(*) FROM t WHERE score > 90

ColumnChunk "score" RowGroup 0:
  min=12, max=85     → 85 ≤ 90 → 整组不可能有 >90 的行 → 直接跳过！省掉 8192 次比较
ColumnChunk "score" RowGroup 1:
  min=45, max=98     → 98 > 90 → 可能有匹配，需要实际扫描
ColumnChunk "score" RowGroup 2:
  min=3,  max=100    → 可能有匹配，需要扫描
```

一次 min/max 比较省掉 8192 次逐行比较——如果数据是按常用过滤列排序/聚簇的（如时间列），zone map 可以跳过 99% 的数据。Parquet/ORC 的行组统计、Delta Lake 的文件级统计，本质都是 zone map。

**铁律**：zone map 是纯粹的优化，**错裁 = 查询结果错误**。不确定时一律返回 true（"不能裁"，只是慢），绝不能返回 false（"裁掉"）。空块（还没有数据）返回 true——"没有信息"等于"不能裁"。

### 从存储到 morsel：Lab1 和 Lab0/Lab3 的衔接

TableData::CreateParallelScanState() 把所有 RowGroup 按 Vector 大小切成 TableScanMorsel 列表：

```
TableData (18000 行, 3 个 RowGroup)
  RG0: 8192 行 → 4 个 morsel (各 2048 行)
  RG1: 8192 行 → 4 个 morsel
  RG2: 1616 行 → 1 个 morsel
                          共 9 个 morsel，id 0..8
                          MorselQueue(9) ← 你 Lab0 写的那个！
```

Lab3 的并行扫描就是：N 个线程调 `NextMorsel()` 抢这 9 个 morsel，拿到 id 就去对应 (row_group, offset, count) 扫描数据。

## Task #1 - ColumnChunk::Append

```cpp
void Append(Vector& data, idx_t source_offset, idx_t count);
```

把 `data[source_offset .. source_offset+count)` 追加到本列块尾部。列块内部是 `std::vector<std::unique_ptr<Vector>> blocks_`。

**必须处理的一般情形**（测试直接命中）：

1. `blocks_` 为空——第一个 Append 需要创建第一个 block
2. 最后一个 block 部分填充（如已有 1500 行，还能装 548）——接着尾部写
3. 一次 Append 跨越**多个** block 边界（从最后一个 block 的位置 2000 追加 200 行 → 填满旧 block 的 48 个 + 新 block 的 152 个）
4. `source_offset` 可以不是 0——数据源本身可能是被 Slice 过的 Vector
5. NULL 和 VARCHAR 必须原样往返——用 `Vector::GetValue(i)` 和 `Vector::SetValue(i, val)` 逐值拷贝即可，Value 内部处理了 NULL 标记和字符串内存

**核心循环骨架**：

```
offset = 0  (在本次 source 数据里的位置)
while (offset < count) {
    if (blocks_ empty 或 最后一个 block 满了) → 追加新 block
    copy_count = min(count - offset, STANDARD_VECTOR_SIZE - 最后block已用行数)
    for i in 0..copy_count-1:
        last_block->SetValue(last_block_used + i, data.GetValue(source_offset + offset + i))
    count_ += copy_count
    offset += copy_count
}
```

**别忘了 zone map**：对每个非 NULL 值调用 `UpdateZoneMap(value)`（T3 的维护部分）。

**测试**：
- `ColumnChunkSingleBlock`：2048 行以内，单个 block
- `ColumnChunkAcrossBlocks`：跨越 3 个 block（含边界对齐读取验证）
- `ColumnChunkNulls`：NULL 值正确往返
- `ColumnChunkVarchar`：字符串类型
- `ColumnChunkPartialAppends`：分 3 次 Append（5+3+2 行）验证增量追加
- `ColumnChunkEmptyScan`：空列块（尚未 Append）Scan 不崩溃、读出 0 行（边界条件）

## Task #2 - ColumnChunk::Scan

```cpp
void Scan(idx_t offset, idx_t count, Vector& out, idx_t out_offset) const;
```

把本列块 `[offset, offset+count)` 的值写入 `out[out_offset ..]`。这是 Append 的反向版，同样需要处理跨 block：

```
Scan(offset=2000, count=200)：

  block_idx = 2000 / 2048 = 0, block_offset = 2000 % 2048 = 2000
  block 0 里可读: min(200, 2048-2000) = 48  → 拷贝 48 个到 out[0..47]
  offset += 48, out_offset += 48, 继续下一个 block
  block_idx = 1, block_offset = 0
  block 1 里可读: min(200-48=152, 2048) = 152 → 拷贝到 out[48..199]
  完成。
```

**Hint**：Append 和 Scan 共享同一套"块索引 = 逻辑位置 / STANDARD_VECTOR_SIZE，块内偏移 = 逻辑位置 % STANDARD_VECTOR_SIZE"的算术。先写 Append 再写 Scan，代码结构几乎对称。

**测试**：和 T1 共享同一组测试（Append 之后必然要 Scan 验证），重点关注 `ColumnChunkAcrossBlocks` 里从倒数第 10 行读 20 行的跨块场景。

## Task #3 - Zone Map

两处工作：

1. **维护**（在 T1 的 Append 循环里）：对每个追加的非 NULL 值调用 `UpdateZoneMap(value)`，更新 `min_`/`max_`：
   - 第一个非 NULL 值：`has_zone_map_ = true`，`min_ = max_ = value`
   - 后续：`value < min_` 则更新 `min_`；`value > max_` 则更新 `max_`

2. **判断**：`CheckZoneMap(constant, comparison)` 返回 `false` 当且仅当谓词 `column OP constant` 对本块**任何行都不可能为真**：

| 谓词 | 可裁剪（返回 false）的条件 | 等号边界（最容易错！） |
|------|---------------------------|----------------------|
| `col = c` | `c < min_` 或 `c > max_` | c 在 [min, max] 内只能说明"可能有"，不能裁剪 |
| `col != c` | 永不裁剪 | min==max==c 理论可裁，本实现不要求 |
| `col < c` | `min_ >= c` | min==c 时所有值 ≥c，无行满足 col<c |
| `col <= c` | `min_ > c` | min==c 时 min 行满足 col<=c，不能裁 |
| `col > c` | `max_ <= c` | max==c 时所有值 ≤c，无行满足 col>c |
| `col >= c` | `max_ < c` | max==c 时 max 行满足 col>=c，不能裁 |

**边界条件速查**（测试逐条检查，务必写对）：

```
设 min=10, max=50：
  col < 10  → false（不可能有行 <10）    ← min >= c
  col < 11  → true（10 不满足，但 11..50 满足，不能裁！）
  col > 50  → false                     ← max <= c
  col >= 10 → true（10 本身满足，不能裁）
  col >= 51 → false                     ← max < c
  col = 30  → true（在区间内，可能有）
  col = 5   → false                     ← c < min
```

**两条铁律**：
- 空块（`has_zone_map_ == false`）永远返回 `true`
- 不确定时返回 `true`——保守不裁只是慢，错裁直接返回错误结果

**测试**：`ZoneMapMinMax`、`ZoneMapPrunesImpossible`、`ZoneMapKeepsPossible`、`ZoneMapBoundaryInclusive`（专门测等号边界）、`ZoneMapVarchar`、`ColumnChunkEmptyScan`。

## Task #4 - RowGroup::Append / RowGroup::Scan

```cpp
void Append(DataChunk& chunk, idx_t source_offset, idx_t count);
void Scan(idx_t offset, idx_t count, const std::vector<idx_t>& column_ids, DataChunk& out) const;
```

行组 = 每列一个 ColumnChunk。这一层是**路由**：

**Append**：遍历 chunk 的每一列 i，转发给 `columns_[i]->Append(chunk.data[i], source_offset, count)`；更新 `count_`。

**Scan**：关键是 `column_ids`——输出的第 j 列来自行组第 `column_ids[j]` 个 ColumnChunk，而不一定是第 j 列！

```
示例：表有 3 列 (id, name, score)
column_ids = {2, 0}  → 只查 score 和 id（name 不读）

out 的第 0 列 ← columns_[2]->Scan(...)  ← score
out 的第 1 列 ← columns_[0]->Scan(...)  ← id
columns_[1] (name) 从头到尾不被触碰！
```

这就是**投影下推（projection pushdown）**：查询计划器把需要的列列表下推到存储层，存储层只读这些列——列存 + 投影下推 = "只读用到的列"。

**测试**：`RowGroupAppendScan`（含 `{1, 0}` 乱序列表验证投影下推：1000 行追加后从 offset=100 扫 5 行，验证列交换后值正确）。

## Task #5 - TableData::Append

```cpp
void Append(DataChunk& chunk);   // 线程安全
```

把任意大小的 DataChunk 追加进表。逻辑：当前（最后一个）行组装到 ROW_GROUP_SIZE(8192) 后**新建行组**继续，直到装完。

```
Append 一个 10000 行的 chunk：

  当前最后一个行组已有 6000 行，CapacityLeft=2192
  第 1 轮：写入 2192 行填满当前 RG → RG 满了
  第 2 轮：新建 RG，写入 min(8192, 10000-2192=7808) = 7808 行 → 新建的 RG 有 7808 行
  完成。总行数 6000+2192+7808=16000 ✓
```

**并发安全**：Lab3 的并行导入会多线程并发调 Append。全程持有 `lock_`（`std::lock_guard<std::mutex>`）即可。行组级别的细粒度并发控制不是本课程重点——存储追加是相对低频的操作，用一个 mutex 简单正确。

**注意**：别忘了类型检查——chunk 的列数和类型必须与表 schema 匹配（参考 answer 中的实现）。

**测试**：
- `TableDataSplitsRowGroups`：插入 2×8192+17=16401 行 → 恰好 3 个行组
- `TableScanMorselsCoverAllRows`：morsel 列表不重不漏覆盖所有行（每个 morsel ≤2048 行）
- `TableScanRoundTrip`：插入数据再扫出来，逐值比对（morsel={0,10,20} 扫 20 行）
- `EmptyTableHasNoMorsels`：空表（0 行 0 行组）CreateParallelScanState 不产生任何 morsel（边界条件！）
- `ConcurrentTableAppendsAreAtomic`：4 线程并发追加各 3000 行，总行数 12000 正确、无丢失无重复

## Testing

```bash
make -j4                               # 编译
./tdbtest Lab1                         # 跑全部 17 个用例
./tdbtest Lab1StorageTest.ZoneMap      # 只跑 zone map 的 5 个
./tdbtest Lab1StorageTest.ColumnChunk  # 只跑 ColumnChunk 的 6 个
```

## Development Hints

- **off-by-one 是这个 Lab 的头号 bug 来源**：写 Scan 之前先在纸上画一次"从 offset=2000 读 200 行"的块索引算术，两个 block 各自读多少
- `Value` 的接口 [value.hpp](file:///Users/bytedance/Projects/tiny-duckdb/src/include/tiny_duckdb/common/value.hpp) 值得通读一遍：`IsNull()`、`GetInteger()`、`GetDouble()`、`GetVarchar()`、`Equals()`、`LessThan()` 后续每个 Lab 都要用
- 调试列存 bug 最直接的方法：在测试里 Append 后 Scan 回来，逐位置 `GetValue(i)` 打印比对——错的表现通常就是"位置 i 的值跑到了位置 j"
- 写完 zone map 后用 `ZoneMapBoundaryInclusive` 专门验证等号归属——这是最容易写错的地方

## Grading Rubric

1. `./tdbtest Lab1` 全部 17 个用例通过；
2. 编译零警告；
3. 不修改测试文件与 `third_party/` 下的任何代码；
4. （自查）zone map 的所有返回 `false` 分支都有测试覆盖——错裁比不裁严重得多。

## 往下看

- **Lab 2（SQL 前端）**：用户写 SQL 时如何表达"只读这几列""过滤 score>90"——这些信息最终变成 ColumnIds 和 TableFilter 下推到你刚写的存储层
- **Lab 3（执行引擎）**：你将看到 morsel 队列怎样把 Task 5 生成的 morsel 列表派发给线程，zone map 怎样在 PhysicalTableScan::GetData 里做整段跳过

## 思考题（不计分）

1. 为什么 zone map 对**有序/聚簇**的数据（如按时间插入的日志表）特别有效，对完全随机洗牌的数据几乎无效？（这解释了为什么数仓建表时要按常用过滤列做聚簇排序。）
2. DuckDB 每个列段还记录 NULL 计数；多了这个信息，`CheckZoneMap` 能处理哪类新谓词？（提示：`col IS NULL`。）
3. VARCHAR 的 min/max 按字典序。对 `col LIKE 'abc%'` 这样的前缀谓词，字典序 zone map 能提供裁剪吗？（提示：前缀匹配等价于范围 `col >= 'abc' AND col < 'abd'`。）
4. 为什么列存在追加路径上比行存慢（写一行要写 N 个列），但在分析场景下仍然远快于行存？
