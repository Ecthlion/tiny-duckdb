# Lab 4（探索性）- lakebase：用 DuckDB 读写湖表

> 对应代码：`lab4_lakebase/lakebase/lake_table.py`（Python，用**真实** DuckDB）
> 对应测试：`cd lab4_lakebase && python3 -m pytest test_lakebase.py -v`（10 个用例）
> 环境：`pip install duckdb pytest`，`python3 -c "import duckdb"` 自检

## Overview

前三个 Lab 我们自己造了一台迷你 DuckDB。本 Lab 转换视角：**把真实 DuckDB 当作执行引擎**，在它之上搭建现代数据栈最流行的形态——湖仓表（lakehouse table）。你将用约 100 行 Python 复刻 Delta Lake 的核心思想，并亲眼看到 Lab 1 的 zone map 在工业界的样子。

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| Task L4.T1 | `create` + `append`：事务日志首提交、DuckDB 写 Parquet | `pytest -k "create or append"` |
| Task L4.T2 | `scan`：快照读 + 投影/过滤下推 | `pytest -k "scan"` |
| Task L4.T3 | `history` + `scan_version`：时间旅行 | `pytest -k "history or time_travel"` |
| Task L4.T4 | `compact`（+ 已提供的 `vacuum`）：小文件合并 | `pytest -k "compact or vacuum"` |

## Background

### 什么是"湖表"？

把一张表直接存成文件系统/对象存储上的一堆 **Parquet 文件**，你就有了一个数据湖。但它缺三样东西，查询引擎不敢放心用：

1. **模式**——列名和类型记在哪？
2. **事务（ACID）**——两个写入者并发追加、读到写了一半的文件怎么办？
3. **快照/时间旅行**——"昨天这张表长什么样"？

Delta Lake / Iceberg / Hudi 的答案惊人地朴素：**再加一个事务日志目录**。表 = 数据文件 + 日志；读表 = 重放日志算出当前活着哪些文件，再让引擎（DuckDB）去读它们。ACID 的物质基础只有一个：一次原子 rename。

### 目录布局

```
lineitem/
  _lake_log/
    00000000000000000000.json   # commit 0: {"actions": [{"schema": ...}]}
    00000000000000000001.json   # commit 1: {"actions": [{"add": {"path": "part-00001-xxx.parquet", ...}}]}
    00000000000000000002.json   # commit 2: ...
  part-00001-xxx.parquet
  part-00002-yyy.parquet
```

每次提交落一个 JSON，记录本组动作：`schema`（表结构）、`add`（新增数据文件）、`remove`（**逻辑**删除）。提交用"写临时文件 + `os.rename`"保证原子性（POSIX rename 原子）。`_commit` / `_snapshot_files` / `version` / `schema` 这些日志机制**已提供**——先读 `lake_table.py` 末尾的 provided helpers，你的四个任务都建立在它们之上。

## Task #1 - create 与 append

`create(path, schema)`：建目录，写下 commit 0（schema 动作，零数据文件）。

`append(rows)`：一次追加 = **一个**新 Parquet 文件 + **一个**提交。流程：

1. 读当前 `self.schema()` 拼出建表 DDL；
2. 把行批塞进一个 DuckDB 临时表（`executemany`），然后 `COPY batch TO '<path>' (FORMAT PARQUET)`——**DuckDB 替你写 Parquet**；
3. `_commit([{"add": {"path": 文件名, "num_rows": len(rows)}}])`；
4. 返回文件名。

文件名建议含提交序号与随机后缀（`part-00001-<uuid8>.parquet`），并发追加时不撞名。**一提交一文件**正是流式写入 Delta 表的真实形态——也是后面"小文件问题"的来源。

**测试**：`test_create_writes_schema_commit`、`test_append_creates_one_parquet_file_per_commit`。

## Task #2 - scan：快照读 + 下推

```python
def scan(self, columns=None, where=None):   # -> list[tuple]
```

1. `_snapshot_files()` 重放日志，得到**当前快照**的活文件列表；空表返回 `[]`；
2. 拼 SQL：`SELECT <投影> FROM read_parquet(?)`（文件列表作为参数传入）；
3. 有 `where` 时**直接拼进 SQL**——DuckDB 会把谓词下推进 Parquet 读取器，利用每个行组内嵌的 min/max 统计整段跳过。**这就是 Lab 1 zone map 的工业版**，而且这次不用你写；
4. 返回所有行。

用 `explain_scan(where=...)`（已提供）亲眼验证：计划里会出现 `Filters: l_orderkey>=295` 字样——过滤发生在扫描层而不是扫描后。

**测试**：`test_scan_all_rows`、`test_scan_projection`、`test_scan_filter_pushdown`（同时断言结果正确与计划含 `Filters:`）。

## Task #3 - history 与 scan_version（时间旅行）

`history()`：逐版本读日志，返回 `[(version, actions), ...]`。

`scan_version(version, ...)`：与 scan 相同，但 `_snapshot_files(at_version=version)` 只重放到指定提交——**当时的**活文件集。因为数据文件只增不删（T4 之前），时间旅行是免费的。

**测试**：`test_history`、`test_time_travel`（含 compact 之后仍能回到 compact 前的版本——这依赖 T4 的语义，见下）。

## Task #4 - compact（OPTIMIZE）与 vacuum

流式写入留下一堆小文件，扫描时要开一堆 reader——臭名昭著的"小文件问题"。`compact()`：

1. 全部活文件 `COPY (SELECT * FROM read_parquet(?)) TO '<新文件>'` 合成一个大文件；
2. **一个提交**里同时 `remove` 全部旧文件、`add` 新文件（原子生效——读者要么看到全旧、要么看到全新）；
3. 返回新文件名；活文件 ≤1 时返回 `None`（no-op）。

**关键语义**：旧文件此时只是**逻辑删除**，物理上保留——所以 compact 之后时间旅行依然有效。真正的物理删除由已提供的 `vacuum()` 完成（删除不在当前快照中的所有文件；Delta 的 VACUUM 相同，只是多一个保留期）。为什么必须"先提交、后由 vacuum 删除"？想想崩溃恢复：compact 若直接删文件，崩溃在提交前/后会分别留下什么烂摊子？

**测试**：`test_compact_merges_files`（逻辑快照只剩一个文件且数据不变）、`test_vacuum_removes_stale_files`、`test_compact_single_file_is_noop`、`test_time_travel`。

## Testing

```bash
cd lab4_lakebase
python3 -m pytest test_lakebase.py -v        # 10 个用例
python3 -m pytest test_lakebase.py -k scan   # 按关键字过滤
python3 demo.py                               # 完整演示：文件、历史、下推计划、时间旅行、compaction、vacuum
```

## Development Hints

- 每一步都可以用 `demo.py` 的方式打印 `_lake_log/` 目录和 `history()` 肉眼核对——湖表的全部状态都在文件系统里，没有黑盒。
- DuckDB 连接很轻量（`duckdb.connect()`，内存库），每个方法随用随开随关即可。
- 调试下推时对比：`explain_scan()` 无 where 的计划里不应有 `Filters:`。

## Grading Rubric

1. 10 个 pytest 用例全部通过；
2. 不修改已提供的日志 helpers 与测试；
3. `append`/`compact` 的提交必须是"先写数据文件、后提交日志"的顺序（反过来会在崩溃后留下日志指向不存在的文件）。

## 思考题（不计分）

- 真实 Delta Lake 的日志还会记录每个文件的**列统计**（min/max/null_count）。这对应我们 Lab 1 的什么机制？加上它，compact 之后 DuckDB 还需要打开哪些文件？
- Iceberg 不"重放 JSON 日志"而用 manifest 清单树，目的是什么？（提示：100 万个 commit 时重放要多久？）
- 要支持 `DELETE FROM t WHERE ...`，基于"只增不删"的文件布局该怎么做？（提示：Delta 的 delete vector / 重写文件两条路线。）
- 真实 DuckDB 还能 `INSTALL delta; SELECT * FROM delta_scan('s3://...')`。有网络时试试，对照你的 `_snapshot_files`——你会发现大家干的是同一件事。
