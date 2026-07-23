# Lab 1 渐进式 Hints

Lab 1 最常见的问题不是 C++ 语法，而是三个坐标系混淆：

- source offset：输入 `Vector` 中的位置；
- logical offset：`ColumnChunk` 中从 0 开始的全局位置；
- block offset：某个 2048 元素 `Vector` 内的位置。

画出边界再写循环，通常比盯着崩溃信息更快。

## L1.T1 `ColumnChunk::Append`

### Hint 1

循环不变量：每轮开始时，`source_index` 指向尚未复制的第一个输入，`remaining` 是尚未复制的数量，`count_` 是目标的下一个空位。

### Hint 2

目标块内偏移是 `count_ % STANDARD_VECTOR_SIZE`。当没有块，或这个余数为 0 时，需要创建新块。

### Hint 3

本轮批量大小同时受两个量限制：

```text
min(remaining, STANDARD_VECTOR_SIZE - target_offset)
```

逐值调用 `GetValue/SetValue`，不要漏掉 NULL；每复制一个值都要交给 zone map 更新。

## L1.T2 `ColumnChunk::Scan`

### Hint 1

对逻辑位置 `p`：

```text
block_index  = p / STANDARD_VECTOR_SIZE
block_offset = p % STANDARD_VECTOR_SIZE
```

### Hint 2

和 Append 一样按“当前块还剩多少空间”分批。额外维护独立的 `out_index`，不要误用输入的逻辑位置作为输出位置。

### Hint 3

若 `ColumnChunkAcrossBlocks` 只在跨边界时失败，打印每轮的 `block_index / block_offset / batch / out_index`，检查推进量是否都等于 `batch`。

## L1.T3 zone map

### Hint 1：维护统计

NULL 不参与 min/max。第一个非 NULL 值要同时初始化 min 与 max，并设置 `has_zone_map_`。

### Hint 2：判断方向

不要问“谓词是否一定为真”，而要问“是否至少可能有一行满足”。只有证明不可能时才返回 `false`。

### Hint 3：边界表

| 谓词 | 仍可能命中的条件 |
|------|------------------|
| `col = c` | `min <= c && c <= max` |
| `col < c` | `min < c` |
| `col <= c` | `min <= c` |
| `col > c` | `max > c` |
| `col >= c` | `max >= c` |

对空块、全 NULL 块或不认识的比较，保守返回 `true`。优化可以少做，不能改错答案。

## L1.T4 `RowGroup`

### Hint 1

Append 没有新的切块逻辑：第 `i` 个输入列转发给第 `i` 个 `ColumnChunk`，所有列使用同一 `source_offset/count`。

### Hint 2

Scan 的输出第 `j` 列来自存储列 `column_ids[j]`。测试传入 `{1, 0}` 是为了抓住把 `j` 直接当列号的错误。

### Hint 3

所有列完成后再设置 `out.SetCardinality(count)`。`count_` 代表行数，只能增加一次，不能每列增加一次。

## L1.T5 `TableData::Append`

### Hint 1

用 `source_offset` 走完整个输入 chunk。每轮只向最后一个行组追加：

```text
space = ROW_GROUP_SIZE - last_group.Count()
batch = min(input_remaining, space)
```

### Hint 2

最后一个行组满了才创建新行组。空表第一次追加也需要创建。

### Hint 3

锁保护的是完整的“检查最后一组空间 → 可能建组 → 追加 → 更新表行数”序列。只给 `row_groups_.push_back` 加锁仍会竞态。

## 失败症状速查

| 症状 | 优先检查 |
|------|----------|
| 只在 2048/4096 边界失败 | `%`、`/` 与 batch 推进 |
| NULL 变成 0/空字符串 | 是否绕过 `Value` 直接拷贝裸内存 |
| zone map 边界测试失败 | `<` 与 `<=` 对应的 min/max 方向 |
| 投影列顺序不对 | `column_ids[j]` |
| 8192 行后丢失/覆盖 | 行组剩余空间与建组时机 |
| 并发追加总行数偶发不对 | 锁的作用域太小 |

推荐按依赖逐步跑：

```bash
./tdbtest Lab1StorageTest.ColumnChunk
./tdbtest Lab1StorageTest.ZoneMap
./tdbtest Lab1StorageTest.RowGroup
./tdbtest Lab1StorageTest
```

