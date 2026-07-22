# Lab 1 - 列式存储

> 对应代码：`src/storage/column_chunk.cpp`、`src/storage/row_group.cpp`、`src/storage/table_data.cpp`
> 测试：`test/lab1_storage_test.cpp`

## 背景：为什么是列存？

分析型查询（OLAP）的典型模式是"扫全表、只读几列"：

```sql
SELECT l_returnflag, sum(l_quantity) FROM lineitem GROUP BY l_returnflag;  -- 18 列里只用 2 列
```

行存（OLTP 的默认）把一行的所有列放在一起，扫 2 列也要把整行读进内存；列存把**每一列单独连续存放**：

```
行存:  [r1c1 r1c2 r1c3][r2c1 r2c2 r2c3]...   读 2 列 = 读全部
列存:  [r1c1 r2c1 ...][r1c2 r2c2 ...]...     读 2 列 = 只碰 2 个列段
```

附带好处：同一列的数据类型相同、取值分布相似，**压缩率**更高；一列连续的数据天然适合**向量化执行**（Lab 3）。

## tiny-duckdb 的存储层级

```
TableData          一张表 = 若干 RowGroup
 └ RowGroup        8192 行（DuckDB 是 122880；调小是为了测试里容易观察到跨行组行为）
    └ ColumnChunk  一个行组的一列 = 若干 Vector + zone map
       └ Vector    2048 个值（STANDARD_VECTOR_SIZE）+ NULL 位图（+ 字符串堆）
```

`Vector`/`DataChunk`（公共层，`src/common/`）是整个系统的血液：执行引擎里流动的数据和存储里落盘的数据，都是同一套向量。

## 任务

### L1.T1 `ColumnChunk::Append`
把 `data[source_offset, source_offset+count)` 追加到列块末尾，空间不够就新开一个 Vector 块。注意三处细节：跨块边界、`ValidityMask`（NULL 位图）、VARCHAR 的字符串需要拷贝。

### L1.T2 `ColumnChunk::Scan`
`Append` 的逆过程：从 `[offset, offset+count)` 读到 `out[out_offset ..]`。

### L1.T3 zone map
每个 ColumnChunk 维护非 NULL 值的 `min/max`，并实现：

```cpp
bool CheckZoneMap(const Value &constant, ExpressionType comparison) const;
```

语义：**仅当 zone map 能证明 `列 比较符 常量` 对块内任何行都不可能为真时返回 false**。逐个比较符想一遍，例如：

| 谓词 | 不可能为真的条件 |
|------|------------------|
| `col = c`  | `c < min` 或 `c > max` |
| `col > c`  | `max <= c` |
| `col >= c` | `max < c` |
| `col < c`  | `min >= c` |

注意边界是闭区间：`col >= max` 是**可能**为真的。Lab 3 的表扫描会用它整片跳过 morsel。

### L1.T4 `RowGroup::Append/Scan`
逐列转发即可，但要维护行数计数与容量检查（ROW_GROUP_SIZE）。

### L1.T5 `TableData::Append`
把任意大小的 DataChunk 切进多个行组：当前行组装满就新建。要线程安全（一把 mutex 即可——追加不是热点，扫描才是）。

## 测试

```bash
./tdbtest   # Lab1StorageTest.* 共 12 个用例
```

## 思考题

- 为什么 zone map 对 `l_returnflag = 'N'` 这类低基数等值谓词几乎没用，对（近似）有序列的范围谓词特别有用？DuckDB 用什么办法让数据"近似有序"？
- 如果一列全是 NULL，`HasZoneMap()` 应该是多少？此时 `CheckZoneMap` 该怎么回答？
