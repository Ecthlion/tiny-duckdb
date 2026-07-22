# Lab 4（探索性）- lakebase：用 DuckDB 读写湖表

> 对应代码：`lab4_lakebase/lakebase/lake_table.py`
> 测试：`cd lab4_lakebase && python3 -m pytest test_lakebase.py -v`
> 运行 `python3 -c "import duckdb"` 确认环境，缺则 `pip install duckdb pytest`

前三个 Lab 我们自己造了一个迷你 DuckDB。本 Lab 换个视角：**用真实的 DuckDB**，探索现代数据栈最流行的形态——湖仓（lakehouse）。

## 背景：什么是"湖表"？

把一张表直接存成对象存储/文件系统上的一堆 **Parquet 文件**，你就有了一个"数据湖"。但它缺三样东西，查询引擎不敢放心用：

1. **模式（schema）**——列名和类型记在哪？
2. **事务（ACID）**——两个写入者同时追加、读到写了一半的文件怎么办？
3. **快照/时间旅行**——"昨天这张表长什么样"？

Delta Lake/Iceberg/Hudi 的答案惊人地朴素：**再加一个事务日志目录**。表 = 数据文件 + 日志；读表 = 重放日志算出当前活着哪些文件，再让引擎（DuckDB）去读它们。本 Lab 我们用 100 行 Python + DuckDB 实现一个迷你 Delta Lake，亲自验证这个思想。

## 目录布局

```
lineitem/
  _lake_log/
    00000000000000000000.json   # commit 0: {"actions": [{"schema": ...}]}
    00000000000000000001.json   # commit 1: {"actions": [{"add": {"path": "part-00001-xxx.parquet", ...}}]}
    ...
  part-00001-xxx.parquet
  part-00002-yyy.parquet
```

每次提交一个 JSON 文件，记录这组动作：`add`（新增数据文件）、`remove`（逻辑删除）、`schema`（表结构）。提交用"写临时文件 + rename"保证原子性（POSIX rename 是原子的——这就是对象存储时代 ACID 的全部物质基础）。

## 任务

### L4.T1 建表与追加
`LakeTable.create(path, schema)` 写下 commit 0；`append(rows)` 做三件事：DuckDB 把行批写成**一个** Parquet 文件（`COPY ... TO ... (FORMAT PARQUET)`）→ 提交一个 `add` 动作 → 返回文件名。一提交一文件，正是流式写入 Delta 表的形态。

### L4.T2 快照读 + 下推
`scan(columns=..., where=...)`：重放日志得到活文件列表，交给 `read_parquet(?)`。把 WHERE 子句拼进 SQL——**DuckDB 会自动把谓词下推进 Parquet 读取器**，利用每个行组的 min/max 统计整段跳过。这正是我们 Lab 1 zone map 的工业版！用 `explain_scan()` 亲眼看计划里的 `Filters:`。

### L4.T3 历史与时间旅行
`history()` 列出全部提交；`scan_version(v)` 只重放到第 v 个提交为止，算出**当时**的活文件集再读。数据文件只增不删（compaction 之前），时间旅行就是免费的。

### L4.T4 compaction（OPTIMIZE）
流式写入会产生大量小文件（"小文件问题"）。`compact()`：把所有活文件用 DuckDB 重写成一个大文件 → 一个提交里同时 `remove` 全部旧文件、`add` 新文件（原子生效）→ **提交成功后**才物理删除旧文件。顺序为什么是"先提交后删除"？想想崩溃恢复：如果在删除前崩溃，表是否仍然正确？

## 测试

```bash
cd lab4_lakebase
python3 -m pytest test_lakebase.py -v   # 10 个用例
python3 demo.py                          # 完整演示：文件、历史、下推计划、时间旅行、compaction
```

## 思考题

- 真实 Delta Lake 的日志还会记录每个文件的**列统计**（min/max/null_count）。这对应我们系统里的什么机制？加了这个，compact 之后时间旅行会发生什么变化？
- Iceberg 不用"重放 JSON 日志"而是用 manifest 清单树，目的是什么？（提示：想想 100 万个 commit 的场景）
- 如果要支持 `DELETE FROM t WHERE ...`，基于"只增不删"的文件布局该怎么做？（提示：Delta 的 delete vector / 重写文件）
- DuckDB 还能直接 `ATTACH 'delta_scan(...)'`、`read_iceberg(...)`。有兴趣可以试 `duckdb` 的 `delta` 扩展（本实验环境无网络，离线即可跳过）。
