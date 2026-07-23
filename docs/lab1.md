# Lab 1 - 列式存储

> 对应代码：`src/storage/column_chunk.cpp`、`src/storage/row_group.cpp`、`src/storage/table_data.cpp`（头文件在 `src/include/tiny_duckdb/storage/`，**任务说明写在头文件里**）
> 对应测试：`test/lab1_storage_test.cpp`（16 个用例）
> 卡住时按任务查看：[Lab 1 渐进式 Hints](hints/lab1-hints.md)

## Overview

这个 Lab 实现 tiny-duckdb 的存储层：列式布局 + 行组切分 + zone map 跳读——分析型数据库区别于事务型数据库（如 BusTub 的 TableHeap）的根本设计。

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| Task L1.T1 | `ColumnChunk::Append`（跨块追加一列数据） | `./tdbtest Lab1StorageTest.ColumnChunk` |
| Task L1.T2 | `ColumnChunk::Scan`（跨块读取一列数据） | 同上（与 T1 一体两面） |
| Task L1.T3 | zone map：维护 min/max + `CheckZoneMap` 裁剪判断 | `./tdbtest Lab1StorageTest.ZoneMap` |
| Task L1.T4 | `RowGroup::Append` / `RowGroup::Scan`（按列路由） | `./tdbtest Lab1StorageTest.RowGroup` |
| Task L1.T5 | `TableData::Append`（行组切分 + 并发安全） | `./tdbtest Lab1StorageTest` |

任务有依赖关系：T1/T2 → T4 → T5，T3 可独立于前两步完成。建议按顺序实现。

## Background

### 为什么是列存？

行存把一行的所有列连续存放——点查"某一整行"最快（OLTP）。但分析型查询的典型形态是"扫 1 亿行，只用其中 3 列"：

```sql
SELECT l_returnflag, avg(l_quantity) FROM lineitem GROUP BY l_returnflag;  -- lineitem 有 16 列
```

列存把**每一列单独连续存放**：扫描时只需读用到的 2 列，其余 14 列一个字节都不碰；同一列类型相同，压缩率高、向量化友好。代价是写放大（插一行要写 N 个列段）和点查整行昂贵——分析场景不在意。

### tiny-duckdb 的存储层级

```
TableData          一张表 = 模式 + 行组列表
 └ RowGroup        水平分区，≤ ROW_GROUP_SIZE (8192) 行     ← Lab 3 并行扫描的调度单位
    └ ColumnChunk  一个行组的一列                            ← 本 Lab 的主战场
       └ Vector    定长块，≤ STANDARD_VECTOR_SIZE (2048) 个值 ← 执行引擎的计算单位
```

两个常量的取值（`common/constants.hpp`）刻意比 DuckDB 小两个数量级（DuckDB 行组 122880 行、向量 2048）：**让你用几千行数据就能观察到多行组、多 morsel 的行为**。

### zone map（区域地图）

每个 ColumnChunk 顺手记录本块数据的 `min`/`max`。查询 `WHERE l_quantity > 100` 遇到一个 max=50 的行组时，**整组跳过**——一次比较省掉 8192 次比较。这是"数据跳过"（data skipping）的最小实现，也是 Parquet/Delta Lake 行组统计的原型（Lab 4 会再见）。

## Task #1 - ColumnChunk::Append

```cpp
void Append(Vector &data, idx_t source_offset, idx_t count);
```

把 `data[source_offset .. source_offset + count)` 追加到本列块尾部。列块内部是 `std::vector<std::unique_ptr<Vector>> blocks_`，每个 block 最多装 STANDARD_VECTOR_SIZE 个值：填满最后一个 block 后新开一个继续，直到装完。

**必须处理的一般情形**：

- `source_offset` 可以是任意值（数据源本身是一个被切片的 Vector）；
- 最后一个 block 可以是半满的（接着上次的尾部继续写）；
- 一次 Append 可以跨越多个 block 边界；
- NULL 和 VARCHAR 必须原样往返——`Vector::GetValue/SetValue` 已经处理了 Value 的全部细节，**逐值拷贝即可，不要试图按类型特化**（那是 Lab 3 的讨论题）。

**Hint**：主循环 `while (remaining > 0)`，每次拷贝 `min(remaining, STANDARD_VECTOR_SIZE - used_in_last_block)` 个值。

**测试**：`ColumnChunkSingleBlock`（单块）、`ColumnChunkAcrossBlocks`（跨 3 块+边界对齐读取）、`ColumnChunkNulls`（NULL 掩码）、`ColumnChunkVarchar`、`ColumnChunkPartialAppends`（5+3+2 三段追加）。

## Task #2 - ColumnChunk::Scan

```cpp
void Scan(idx_t offset, idx_t count, Vector &out, idx_t out_offset) const;
```

把本列块 `[offset, offset+count)` 的值写入 `out[out_offset ..]`。就是 Append 的反向版，同一套边界算术。`ColumnChunkAcrossBlocks` 专门构造了"读取区间跨越两块"的情形（从倒数第 10 行读 20 行）。

**Hint**：先写 Append 再写 Scan；两个函数可以用同一副"块索引 = offset / STANDARD_VECTOR_SIZE，块内偏移 = offset % STANDARD_VECTOR_SIZE"骨架。

## Task #3 - zone map

两处工作：

1. **维护**：`Append` 里对每个非 NULL 值调用 `UpdateZoneMap`，更新 `min_`/`max_`（`Value::LessThan` 比较；首个值把 `has_zone_map_` 置真）。
2. **判断**：`CheckZoneMap(constant, comparison)` 当且仅当谓词 `column OP constant` 对本块**任何行都不可能为真**时返回 `false`。

六种比较的完整裁剪条件（边界是最容易错的地方，测试逐条检查）：

| 谓词 | 可裁剪（返回 false）的条件 | 注意 |
|------|---------------------------|------|
| `col = c` | `c < min` 或 `c > max` | 区间内只说明"可能"，不能裁剪 |
| `col != c` | 永不裁剪（本实现） | min=max=c 理论上可裁，不要求 |
| `col < c` | `min >= c` | 等于号归属：min==c 时无行满足 |
| `col <= c` | `min > c` | min==c 时 min 行满足 |
| `col > c` | `max <= c` | max==c 时无行满足 |
| `col >= c` | `max < c` | max==c 时 max 行满足 |

**两条铁律**：

- **空块**（`has_zone_map_ == false`）永远返回 `true`——"没有信息"意味着"不能裁剪"，绝不能反过来裁掉一切；
- 不确定时一律返回 `true`：zone map 是纯粹的优化，**错裁 = 查询结果错误**，保守不错裁 = 只是慢。

**测试**：`ZoneMapMinMax`、`ZoneMapPrunesImpossible`、`ZoneMapKeepsPossible`、`ZoneMapBoundaryInclusive`、`ZoneMapVarchar`、`ColumnChunkEmptyScan`。

## Task #4 - RowGroup::Append / RowGroup::Scan

```cpp
void Append(DataChunk &chunk, idx_t source_offset, idx_t count);
void Scan(idx_t offset, idx_t count, const std::vector<idx_t> &column_ids, DataChunk &out) const;
```

行组 = 每列一个 ColumnChunk。Append 把 chunk 的第 i 列转发给 `columns_[i]`，维护好 `count_`。Scan 的关键是 **`column_ids`**：输出第 j 列来自行组第 `column_ids[j]` 列——查询引擎靠它实现"只读用到的列"（投影下推到存储层）。`RowGroupAppendScan` 用 `{1, 0}` 的乱序列表演示了这一点。

## Task #5 - TableData::Append

```cpp
void Append(DataChunk &chunk);   // 线程安全
```

把任意大小的 DataChunk 追加进表：当前（最后一个）行组装到 ROW_GROUP_SIZE 后**新建行组**继续，直到装完。Lab 3 的并行导入会并发调用它——全程持有 `lock_`（std::mutex）即可，行组级别的并发控制不是本课程重点。

**测试**：`TableDataSplitsRowGroups`（2×ROW_GROUP_SIZE+17 行 → 恰好 3 个行组）、`TableScanMorselsCoverAllRows`（morsel 列表不重不漏）、`TableScanRoundTrip`。

## Testing

```bash
./tdbtest Lab1                          # 16 个用例
./tdbtest Lab1StorageTest.ZoneMap       # 只跑 zone map 的 7 个
```

## Development Hints

- 写 `Scan` 之前先在纸上画一次"跨两块读 20 行"的指针算术；这个 Lab 的 bug 九成是 off-by-one。
- 常用调试手段：在测试里直接 `table.Scan(...)` 后打印 `Vector::GetValue` 逐项比对——列存 bug 的表现就是"位置 i 的值跑到了位置 j"。
- `Value` 的接口（`common/value.hpp`）值得通读一遍：`IsNull / GetInteger / GetDouble / GetVarchar / Equals / LessThan / Hash` 后续每个 Lab 都要用。

## Grading Rubric

1. `./tdbtest Lab1` 全部通过；
2. 编译零警告；
3. 不修改测试文件与 `third_party/` 下的任何代码；
4. （自查）zone map 的所有返回 `false` 分支都有测试覆盖——错裁比不裁严重得多。

## 思考题（不计分）

- 为什么 zone map 对**有序/聚簇**的数据特别有效，对随机洗牌的数据几乎无效？（这解释了为什么数仓要按常用过滤列排序。）
- DuckDB 每个列段还记录 NULL 计数；多了它，`CheckZoneMap` 能处理哪类新谓词？
- 本实现 VARCHAR 的 min/max 按字典序。对 `col LIKE 'abc%'` 这样的谓词，字典序 zone map 还能提供裁剪吗？
