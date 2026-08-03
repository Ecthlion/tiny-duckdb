# Lab 4（探索性）- LakeBase：用 DuckDB 读写湖表

> 对应代码：`lab4_lakebase/lakebase/lake_table.py`（Python，用**真实 DuckDB**）
> 对应测试：`cd lab4_lakebase && python3 -m pytest test_lakebase.py -v`（10 个用例）
> 环境：`pip install duckdb pytest`，`python3 -c "import duckdb"` 自检

## Overview

前三个 Lab 我们**自己造**了一台迷你 DuckDB——内存中的列存、push-based 执行器、morsel 驱动并行。本 Lab 转换视角：**把真实 DuckDB 当作执行引擎**，在它之上搭建现代数据栈最流行的形态——**湖仓表（lakehouse table）**。

你将用约 100 行 Python 复刻 Delta Lake 的核心思想：Parquet 数据文件 + JSON 事务日志 + 原子 commit = ACID 语义的表。做完之后你会亲眼看到：Lab 1 里我们手写的 zone map，工业界叫 Parquet row group statistics，DuckDB 的 `read_parquet` 直接用它做下推——核心原理一模一样。

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| T1 | `create` + `append`：事务日志首提交、DuckDB 写 Parquet | `pytest -k "create or append"` |
| T2 | `scan`：快照读 + 投影/过滤下推 | `pytest -k "scan"` |
| T3 | `history` + `scan_version`：时间旅行 | `pytest -k "history or time_travel"` |
| T4 | `compact` + 已提供的 `vacuum`：小文件合并 | `pytest -k "compact or vacuum"` |

**推荐顺序**：T1 → T2 → T3 → T4。T1 建表加数据，T2 验证能读（下推是顺带的），T3 验证历史，T4 解决小文件问题。

---

## Background

### 0. 从内存表到湖表——Lab 1 vs Lab 4 对比

| 维度 | Lab 1 内存表 (TableData) | Lab 4 湖表 (LakeTable) |
|------|------------------------|----------------------|
| 存储位置 | 进程内存（C++ 对象） | 文件系统（Parquet + JSON） |
| 持久性 | 进程退出即消失 | 持久化到磁盘 |
| 事务 | 无（单进程教学用） | 原子 commit（POSIX rename） |
| 时间旅行 | 不支持 | 支持（快照重放） |
| 并发写入 | Append 有锁但无 ACID | 单写者原子提交（简化版） |
| 数据格式 | 自定义 ColumnChunk | Parquet（开放标准） |
| Zone map | 自己手写 min/max | Parquet 自带，DuckDB 自动用 |
| 读取引擎 | 自己写的 TableScan | DuckDB 的 `read_parquet` |
| 小文件问题 | 不存在（内存连续） | 必须 compact |

Lab 4 是把 Lab 1 的列存思想放到**真实的文件系统+真实查询引擎**上，补上工业界必需的事务和版本管理。

### 1. 什么是"湖表"？

把一张表直接存成文件系统/对象存储上的一堆 **Parquet 文件**，你就有了一个数据湖。但它缺三样关键的东西，查询引擎不敢放心用：

```
裸 Parquet 文件堆（数据湖）的问题：
═════════════════════════════════
  lineitem/
    part-001.parquet  ← 谁写的？什么时候？列名对吗？
    part-002.parquet  ← 两个 writer 同时写怎么不冲突？
    tmp-xyz.parquet   ← 这是写了一半的文件吗？能读吗？
    part-003.parquet  ← 昨天删了的文件怎么又出现了？
```

缺的三样：
1. **Schema（模式）**——列名和类型记在哪？
2. **ACID 事务**——两个写入者并发追加、读到写了一半的文件怎么办？
3. **快照/时间旅行**——"昨天这张表长什么样"？审计/回滚怎么做？

Delta Lake / Iceberg / Hudi 的答案惊人地朴素：**再加一个事务日志目录**。

```
湖表 = 数据文件（Parquet） + 事务日志（JSON）
═══════════════════════════════════════════
  读表 = 重放日志算出当前"活着哪些文件"
       → 让 DuckDB 读这些活文件

  ACID 的物质基础 = 一次原子 rename（POSIX 保证）
```

这就是整个湖仓架构的核心思想，复杂功能（schema evolution、DELETE、UPDATE、Z-Order）都建立在这个简单基础上。

### 2. 目录布局与文件命名

```
lineitem/                              ← 表根目录
├── _lake_log/                         ← 事务日志目录
│   ├── 00000000000000000000.json     ← commit 0（schema）
│   ├── 00000000000000000001.json     ← commit 1（add part-00001-...parquet）
│   ├── 00000000000000000002.json     ← commit 2（add part-00002-...parquet）
│   └── 00000000000000000003.json     ← commit 3（add part-00003, remove ...）
├── part-00001-<uuid8>.parquet        ← 数据文件
├── part-00002-<uuid8>.parquet
└── part-00003-<uuid8>.parquet         ← compact 后的大文件
```

**文件命名规则**：
- 日志文件：20 位零填充的版本号 `.json`（如 `00000000000000000003.json` = version 3）；
- 数据文件：`part-{提交序号5位}-<8位uuid>.parquet`（如 `part-00003-a1b2c3d4.parquet`）。uuid 保证并发追加时不撞名。

### 3. Commit 日志格式

每个 commit 是一个 JSON 文件，包含一组 action（动作）：

```json
{
  "version": 3,
  "actions": [
    {"add":    {"path": "part-00003-a1b2c3d4.parquet", "num_rows": 100}},
    {"remove": {"path": "part-00001-xxxx.parquet"}},
    {"remove": {"path": "part-00002-yyyy.parquet"}}
  ]
}
```

三种 action：
| Action | 语义 |
|--------|------|
| `{"schema": {"columns": {...}}}` | 建表时记录列名和类型（仅 commit 0） |
| `{"add": {"path": ..., "num_rows": N}}` | 新增一个数据文件（写入活跃集） |
| `{"remove": {"path": ...}}` | **逻辑删除**一个文件（从活跃集移除，但文件物理还在） |

**一个 commit 可以包含多个 add 和多个 remove，它们原子生效**——这是 compact 的关键。

### 4. 原子 Commit 协议（已提供）

框架提供的 `_commit(actions)` 方法实现了原子提交：

```
_commit(actions) 流程：
═══════════════════════
1. version = self.version() + 1   ← 当前版本号+1
2. tmp_path = _log_file(version) + ".tmp"   ← 临时文件
3. 把 {"version": version, "actions": actions} 写入 tmp_path
4. os.rename(tmp_path, _log_file(version))   ← 原子 rename！
```

**为什么是原子的？** POSIX 保证 `rename(old, new)` 是原子操作：要么 new 完整出现（commit 成功），要么不出现（commit 失败）。不存在"写了一半的日志文件"。读者要么看到旧版本，要么看到新版本，永远不会看到中间状态。

```
Commit 时序图（崩溃场景分析）：
═════════════════════════════════

  Writer                          Reader
  ──────                          ──────
  写 tmp 文件...
    (此时 tmp 存在但 .json 不存在)
    ↓                             _snapshot_files() 扫描 .json
  rename tmp → 003.json            → 只看到 000,001,002（旧版本）
    ↓ (原子！)                     → 不会看到 003.json 的一半
                                  _snapshot_files()
                                    → 看到完整的 003.json ✅
```

**提交顺序铁则**：**先写数据文件，后写 commit 日志**。

```
正确顺序：
  ① COPY 数据到 part-xxx.parquet（数据文件已落盘）
  ② _commit([add action])         （日志指向存在的文件）
  ✅ 崩溃在①之后、②之前：有孤儿数据文件，但没有日志指向它→无害
  ✅ 崩溃在②之后：commit 完成，文件存在→正确

错误顺序（反过来）：
  ① _commit([add action])         （日志指向尚不存在的文件）
  ② COPY 数据到 part-xxx.parquet
  ❌ 崩溃在①之后、②之前：日志指向不存在的文件→读者报错！
```

### 5. 快照重放——`_snapshot_files()`（已提供）

读表的第一步是重放日志，算出"当前版本哪些文件是活的"。`_snapshot_files(at_version=None)` 已实现：

```
_snapshot_files 算法：
══════════════════════
live = []
for version in 1..latest:           # commit 0 只有 schema，跳过
    for action in commit[version].actions:
        if add action:
            live.append(path)       # 文件进入活跃集
        elif remove action:
            live.remove(path)       # 文件离开活跃集（逻辑删除）
return live
```

**重放示例**（append × 2，然后 compact）：

```
commit 0: schema                        → live = []
commit 1: add part-00001-aaa.parquet    → live = [aaa.parquet]
commit 2: add part-00002-bbb.parquet    → live = [aaa.parquet, bbb.parquet]
commit 3: add ccc.parquet               ←──┐
          remove aaa.parquet               │ compact: 合并成一个新文件
          remove bbb.parquet            ←──┘
                                        → live = [ccc.parquet]
```

快照重放是个**追加日志的状态机**：从空集开始，逐步应用 add/remove。时间旅行（T3）就是在指定版本停下——比如 `at_version=2` 时只重放到 commit 2，live = [aaa, bbb]，就能读到 compact 前的数据。

> 💡 **性能注记**：每次读都重放所有日志，在 commit 数很多（比如 100 万）时会很慢。Iceberg 用 manifest 清单树、Delta Lake 用 checkpoint 来解决这个问题——但对于这个教学 Lab，重放完全够用。

### 6. Parquet 和 Zone Map——Lab 1 在工业界的样子

Parquet 是一种**列存文件格式**（和我们 Lab 1 的 ColumnChunk 设计思想一致）。文件内部按 row group 切分（默认约 100 万行），每个 row group 中每列是一个 column chunk，每个 column chunk 内嵌了 min/max/null_count 统计信息——**这就是 zone map**。

```
Parquet 文件内部结构：
══════════════════════
part-0001-xxx.parquet
├── Row Group 0 (~100K 行)
│   ├── Column Chunk 0 (l_orderkey, INTEGER)
│   │   ├── 数据页（压缩的整数序列）
│   │   └── 统计: min=0, max=99999, null_count=0
│   ├── Column Chunk 1 (l_quantity, DOUBLE)
│   │   ├── 数据页
│   │   └── 统计: min=0.0, max=49.99, null_count=0
│   └── Column Chunk 2 (l_returnflag, VARCHAR)
│       ├── 数据页
│       └── 统计: min="A", max="R", null_count=0
├── Row Group 1 (~100K 行)
│   └── ...（每个 RG 各有自己的统计）
└── Footer（元数据：schema、RG 偏移、统计信息目录）
```

当 DuckDB 执行 `SELECT * FROM read_parquet(?) WHERE l_orderkey >= 495` 时，它读取 footer 拿到各 row group 的 min/max，跳过所有 `max < 495` 的 row group——和我们 Lab 1 TableScan 里的 zone map 检查**完全一样**。`explain_scan(where=...)` 的输出里出现 `Filters: l_orderkey>=495` 就证明下推生效了。

---

## Task #1 - `create` 与 `append`

### T1a `LakeTable.create(path, schema)`

静态方法，创建一个新的湖表：
1. `os.makedirs(os.path.join(path, "_lake_log"), exist_ok=True)` 建表目录和日志目录；
2. 构造 LakeTable 实例；
3. 调 `_commit()` 写入 commit 0，action 类型为 `schema`：
   ```python
   {"schema": {"columns": schema}}
   ```
   注意：commit 0 没有 add/remove action，只有 schema。
4. 返回 table 实例。

### T1b `append(rows)`

追加一批行（一个 rows 列表，每个元素是 tuple），**一次追加 = 一个新 Parquet 文件 + 一个 commit**：

1. 读取当前 schema（`self.schema()` 返回 `{列名: 类型字符串}` dict）；
2. 构造唯一文件名：`part-{version+1:05d}-{uuid8}.parquet`（uuid 取 `uuid.uuid4().hex[:8]`）；
3. 用 DuckDB 写 Parquet：
   ```python
   con = duckdb.connect()   # 内存连接，轻量
   # 建临时表
   cols = ", ".join(f"{name} {typ}" for name, typ in self.schema().items())
   con.execute(f"CREATE TEMPORARY TABLE batch ({cols})")
   # 插入数据
   placeholders = ", ".join(["?"] * len(self.schema()))
   con.executemany(f"INSERT INTO batch VALUES ({placeholders})", rows)
   # 导出 Parquet
   full_path = os.path.join(self.path, filename)
   con.execute(f"COPY batch TO '{full_path}' (FORMAT PARQUET)")
   con.close()
   ```
4. **先写好数据文件，再 commit**：
   ```python
   self._commit([{"add": {"path": filename, "num_rows": len(rows)}}])
   ```
5. 返回文件名。

**测试**：`test_create_writes_schema_commit`（version=0，schema 正确）、`test_append_creates_one_parquet_file_per_commit`（append 两次 → 2 个 parquet 文件，version=2）。

---

## Task #2 - `scan`：快照读 + 下推

```python
def scan(self, columns=None, where=None) -> list[tuple]
```

1. 调 `self._snapshot_files()` 获取当前快照的活文件列表（相对路径）；
2. 空表（live 为空）直接返回 `[]`；
3. 把相对路径转成绝对路径列表；
4. 构造 SQL：
   - 投影列：`columns=None` 时 `SELECT *`，否则 `SELECT col1, col2, ...`；
   - FROM 子句：`FROM read_parquet(?)`，文件列表作为 SQL 参数（DuckDB 支持传入 list）；
   - WHERE 子句：有 where 时直接拼到 SQL 末尾：`WHERE <where字符串>`；
5. 用 DuckDB 执行查询，`fetchall()` 返回所有行；
6. 关闭连接，返回结果。

**示例生成的 SQL**：
```python
# scan() → 读所有列、无过滤
"SELECT * FROM read_parquet(?)"

# scan(columns=["l_returnflag"]) → 只投影一列
"SELECT l_returnflag FROM read_parquet(?)"

# scan(where="l_orderkey >= 495") → 过滤下推
"SELECT * FROM read_parquet(?) WHERE l_orderkey >= 495"

# scan(columns=["l_returnflag"], where="l_quantity > 10.0")
"SELECT l_returnflag FROM read_parquet(?) WHERE l_quantity > 10.0"
```

**为什么直接拼 where 字符串是安全的**？这是教学简化——where 不是来自用户输入，而是教学测试里写死的字符串。真实系统会做 SQL 注入防护，但在这个 Lab 里直接拼接即可。

**下推验证**：`explain_scan(where="l_orderkey >= 495")` 输出包含 `Filters: l_orderkey>=495` 字样，说明过滤不是"读全部再过滤"，而是在 Parquet 扫描层就利用 row group 统计跳过了。

**测试**：`test_scan_all_rows`（两次 append 共 20 行全部读出）、`test_scan_projection`（只投影一列，每行 tuple 长度为 1）、`test_scan_filter_pushdown`（500 行里过滤出 5 行 + plan 含 Filters）。

---

## Task #3 - `history` 与 `scan_version`：时间旅行

### T3a `history()`

返回表的全部历史版本信息：
1. 从 version 0 到 `self.version()` 遍历每个 commit；
2. 对每个版本调 `self._read_log(v)` 读日志；
3. 返回 list of tuple：`[(version, actions), ...]`。

比如 append 两次后 history() 返回：
```python
[
    (0, [{"schema": {"columns": {...}}}]),
    (1, [{"add": {"path": "part-00001-...parquet", "num_rows": 10}}]),
    (2, [{"add": {"path": "part-00002-...parquet", "num_rows": 10}}]),
]
```

### T3b `scan_version(version, columns=None, where=None)`

时间旅行：读取指定版本的快照。实现和 `scan` 几乎一样，唯一区别是活文件列表来自：
```python
files = self._snapshot_files(at_version=version)
```

其余逻辑（拼 SQL、执行、返回）完全相同。

**为什么时间旅行在 compact 后仍然有效**？因为 compact 只是对旧文件做了 **remove action（逻辑删除）**，文件物理上还在磁盘上。`_snapshot_files(at_version=2)` 重放到 commit 2 就停了，根本看不到 commit 3 的 remove action，所以活文件列表里仍然有旧文件——这就是测试 `test_time_travel` 在 compact 之后仍然能 scan_version(2) 的原因。

**测试**：`test_history`（版本列表正确，commit 1 的第一个 action 是 add）、`test_time_travel`（version 1 有 5 行，version 2 有 10 行，compact 后 version 2 依然可读）。

---

## Task #4 - `compact`：小文件合并（OPTIMIZE）

流式追加（每次 append 一个文件）会留下一堆小 Parquet 文件。比如 100 次 append 各写 10 行，就有 100 个小文件。扫描时 DuckDB 要打开 100 个 reader，I/O 开销巨大——这就是臭名昭著的**小文件问题**。

`compact()` 把所有活文件合并成一个大文件：

1. `live = self._snapshot_files()` 获取当前活文件列表；
2. 如果 `len(live) <= 1`，返回 `None`（no-op，无需合并）；
3. 构造 compact 后的新文件名（参考实现用 `part-compacted-{uuid8}.parquet`，命名规则不做强制要求，只要不与现有文件冲突即可）；
4. 用 DuckDB 把所有活文件读出来，写入一个新 Parquet：
   ```python
   con = duckdb.connect()
   # 所有活文件的绝对路径
   live_paths = [os.path.join(self.path, f) for f in live]
   new_path = os.path.join(self.path, new_filename)
   # 一个 SQL 搞定：读取所有活文件 → 写入单个新 Parquet
   con.execute(f"COPY (SELECT * FROM read_parquet(?)) TO '{new_path}' (FORMAT PARQUET)",
               [live_paths])
   con.close()
   ```
5. **构造原子 commit**：一个 commit 里同时包含：
   - 一个 add action：新文件；
   - N 个 remove action：每个旧活文件一个。
   ```python
   actions = [{"add": {"path": new_filename, "num_rows": total_rows}}]
   for old_file in live:
       actions.append({"remove": {"path": old_file}})
   self._commit(actions)
   ```
6. 返回新文件名。

### Compact 的原子性保证

```
compact 时序（关键：一个 commit 里同时 add + remove）：
═══════════════════════════════════════════════════════
  ① 读所有活文件 → 写入新的大文件 new.parquet（此时新旧文件共存）
  ② commit: add new.parquet + remove old1, remove old2, ...
     └─ 这个 commit 原子生效：
        读者要么看到 commit 前：[old1, old2, ...]（旧状态）
        读者要么看到 commit 后：[new.parquet]    （新状态）
        永远不会看到 "new.parquet + 部分 old" 的中间状态！
```

### Vacuum：物理删除（已提供）

`vacuum()` 做真正的物理删除：列出表目录下所有 `.parquet` 文件，把**不在当前快照中的**删掉。这一步会**破坏时间旅行**——被删除的文件对应的旧版本再也读不到了。

```
compact 和 vacuum 的区别：
══════════════════════════
  compact → 逻辑删除（remove action），文件还在磁盘上 → 时间旅行有效
  vacuum  → 物理删除（os.remove），文件永久消失    → 时间旅行失效
```

Delta Lake 的 VACUUM 有一个默认的 retention period（比如 7 天），防止误删还需要的旧文件；我们这里直接删，简化教学。

### 崩溃恢复分析

**问题**：为什么 compact 不能"先删旧文件，再写新文件，再 commit"？

```
❌ 错误顺序：
  ① os.remove(old1.parquet)
  ② os.remove(old2.parquet)    ← 崩溃！文件已删，新文件没写
  ③ 写 new.parquet
  ④ commit
  结果：所有数据丢失！

✅ 正确顺序（我们的实现）：
  ① 写 new.parquet              ← 崩溃？只是多了个孤儿文件，无害
  ② commit(add new, remove old1, remove old2)
                                 ← 崩溃？old1,old2 还在，可以继续用
  ③ vacuum() 可在任意安全时刻后运行
```

```
各阶段崩溃后的状态：
─────────────────────────────────────────
崩溃点                   数据可恢复？  处理方式
写 new.parquet 中途      ✅ 是        旧文件完好，日志未更新；
                     孤儿 tmp 文件无害
commit 之前              ✅ 是        同上；new.parquet 是孤儿但无引用
commit 之后              ✅ 是        新快照生效，旧文件逻辑删除但仍在
vacuum 中途              ⚠️ 部分      已删的物理消失，未删的还在；
                     不影响当前快照
```

**测试**：
- `test_compact_merges_files`（4 次 append 后 compact，活文件只剩 1 个，数据完整 100 行）；
- `test_compact_single_file_is_noop`（只有 1 个文件时 compact 返回 None）；
- `test_vacuum_removes_stale_files`（compact 后 vacuum 删除 3 个旧文件，只剩 1 个，数据完整）；
- `test_time_travel`（compact 后 version 2 仍然可读，验证了逻辑删除≠物理删除）。

---

## Testing

```bash
cd lab4_lakebase
python3 -m pip install duckdb pytest    # 首次安装依赖
python3 -m pytest test_lakebase.py -v   # 10 个用例全跑
python3 -m pytest test_lakebase.py -k scan    # 按关键字过滤
python3 demo.py                          # 完整演示：文件、历史、下推计划、时间旅行、compaction、vacuum
```

### 里程碑自检

| 完成 | 可用功能 | 验证 |
|------|---------|------|
| T1 | CREATE TABLE + INSERT（append） | test_create, test_append |
| T1-T2 | SELECT（含投影、WHERE 下推） | test_scan* |
| T1-T3 | SELECT AS OF VERSION（时间旅行） | test_history, test_time_travel |
| T1-T4 | OPTIMIZE（compact）+ VACUUM | test_compact*, test_vacuum* |

---

## Development Hints

1. **看文件系统**：湖表的全部状态都在文件系统上，没有黑盒。任何时候 `ls -la my_table/` 和 `cat my_table/_lake_log/*.json` 都能看到真相。`demo.py` 就是这么做的。
2. **DuckDB 连接很轻量**：`duckdb.connect()` 创建的是内存数据库，每个方法里随用随开随关（`con.close()`）即可，不需要复用连接。
3. **路径处理**：数据文件路径在日志中存的是**文件名（basename）**，传给 DuckDB 时要用 `os.path.join(self.path, filename)` 拼成绝对路径。`_snapshot_files()` 返回的也是 basename。
4. **调试下推**：先无 where 调 `explain_scan()`，确认计划里没有 `Filters:`；加 where 后再调，确认出现 `Filters:`——这就是下推生效的证据。
5. **num_rows 统计**：add action 里的 `num_rows` 字段在测试里不验证具体值（只检验逻辑正确），填 `len(rows)` 即可。
6. **schema 列顺序**：DuckDB 的 CREATE TABLE 和 INSERT 按字典序或定义序处理列名，但我们用 `self.schema().items()` 按列名遍历即可（测试的数据构造也按 schema 顺序对应 tuple）。

---

## Grading Rubric

1. 10 个 pytest 用例全部通过；
2. 不修改已提供的日志 helpers（`_commit`、`_snapshot_files`、`version`、`schema`、`_read_log`、`_log_file`、`vacuum`、`explain_scan`）与测试文件；
3. `append` 和 `compact` 必须遵循"先写数据文件、后 commit 日志"的顺序（崩溃安全）；
4. `compact` 的 commit 必须在一个原子提交里同时 add 新文件和 remove 全部旧文件。

---

## 进阶练习（不计分）

1. **分区表**：在 append 时按某列值（如日期）创建子目录（`date=2024-01-01/part-xxx.parquet`），scan 时利用分区剪枝——如果 WHERE 子句包含分区列，直接跳过不匹配的子目录。这比 Parquet row group 级别的 zone map 更粗粒度但更高效。
2. **Delta Lake 统计信息**：在 add action 里加上每列的 min/max/null_count（和 Parquet footer 里的一致），在 scan 时根据 where 条件提前剪枝文件——如果文件级别的 min/max 就能判定"整个文件不可能命中"，就不把它放进 read_parquet 的文件列表。
3. **并发写**：当前实现是单写者。用 `os.rename` 的原子性实现乐观并发控制（OCC）：写之前记录当前版本号，commit 时如果日志里已有新版本（有人抢先了），就 abort 重试。这就是 Delta Lake 的 optimistic concurrency control。
4. **DELETE 支持**：实现 `delete(where)` 。两种路线：(a) 写一个 delete vector（位图标记哪些行被删除），读的时候过滤掉；(b) 重写受影响的文件（remove 旧文件、add 新文件，新文件里不包含被删行）。两种路线各有什么优劣？

---

## 思考题（不计分）

- 真实 Delta Lake 在 commit 前会先写一个 `_last_checkpoint` 文件，读者可以从 checkpoint 开始读而不是从 commit 0 重放。在什么情况下 checkpoint 是必要的？（提示：每天 1000 次 commit，一年后要重放多少个日志文件？）
- Iceberg 不用"重放日志"而用 manifest 树：一个 snapshot 文件指向所有 manifest 文件，每个 manifest 列出一组数据文件。相比重放 JSON 日志，这种设计有什么优势和劣势？
- Parquet 的 row group 大小默认 100 万行。太大（比如 1 亿）或太小（比如 1000）各有什么问题？（参考：Lab 3 的 STANDARD_VECTOR_SIZE=2048 的权衡。）
- 对象存储（S3）上的 rename 不是原子的（实际是 copy + delete）。基于 S3 的湖表要怎么实现原子 commit？（提示：Delta Lake on S3 用 DynamoDB；Iceberg 用的是"先写新 metadata 再原子切换指针"。）
- Lab 3 我们学了 morsel-driven 并行——每个线程从 MorselQueue 领 morsel。在湖表场景下，"morsel" 是什么粒度？如果一个 Parquet 文件很大，怎么切成 morsel 分给多线程？
- 为什么 vacuum 有 retention period？如果可以随时 vacuum，会有什么风险？（提示：并发读 + 并发 vacuum。）
