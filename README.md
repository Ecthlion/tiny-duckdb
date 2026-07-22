# tiny-duckdb

> 一个用于学习 OLAP 数据库内核的教学项目：参考 [BusTub](https://github.com/cmu-db/bustub)（CMU 15-445 的 OLTP 教学数据库）的课程设计，把 [DuckDB](https://github.com/duckdb/duckdb) 的核心架构提炼成 5 个循序渐进的 Lab。

DuckDB 很强，但历经多个版本后代码量太大，直接读源码学习"一个经典 OLAP 数据库应该如何实现"非常困难。tiny-duckdb 从零开始、仅保留最核心的架构概念，用约 5000 行 C++17 实现一个**真的可以跑 SQL** 的分析型数据库：

```
$ ./tiny_duckdb_shell
tdb> CREATE TABLE lineitem (l_orderkey INTEGER, l_quantity DOUBLE, l_returnflag VARCHAR);
tdb> INSERT INTO lineitem VALUES (1, 17.0, 'N'), (2, 36.0, 'A'), (3, 8.0, 'N');
tdb> SELECT l_returnflag, count(*), avg(l_quantity) FROM lineitem GROUP BY l_returnflag;
| l_returnflag | count(l_star) | avg(l_quantity) |
|--------------|---------------|-----------------|
| N            | 2             | 12.5            |
| A            | 1             | 36              |
(2 rows)
```

## 架构总览

```
        SQL 文本
           │
     ┌─────▼─────┐   Lab 2   PEG 解析器（自研 packrat 引擎，无需第三方库）
     │  Parser   │
     └─────┬─────┘
           │ AST
     ┌─────▼─────┐   Lab 2   名称解析、类型检查、聚合重写
     │  Binder   │
     └─────┬─────┘
           │ 逻辑计划
     ┌─────▼─────┐   Lab 3   谓词下推、物理算子选择
     │  Planner  │
     └─────┬─────┘
           │ 物理计划
     ┌─────▼─────┐   Lab 3   morsel-driven 并行的 push-based 流水线
     │ Executor  │
     └─────┬─────┘
           │
     ┌─────▼─────┐   Lab 1   列存、行组、zone map
     │  Storage  │
     └───────────┘
```

## Lab 列表

| Lab | 主题 | 关键概念 | 文档 |
|-----|------|----------|------|
| **Lab 0** | C++ 热身 | 原子操作、morsel 队列 | [docs/lab0.md](docs/lab0.md) |
| **Lab 1** | 列式存储 | Vector、DataChunk、行组、zone map 跳读 | [docs/lab1.md](docs/lab1.md) |
| **Lab 2** | SQL 前端 | PEG 文法、packrat 解析、Transformer、Binder | [docs/lab2.md](docs/lab2.md) |
| **Lab 3** | 执行引擎 | push-based 流水线、morsel-driven 并行、向量化执行、哈希聚合/连接 | [docs/lab3.md](docs/lab3.md) |
| **Lab 4** | 探索性：lakebase | 用真实 DuckDB 读写 Parquet 湖表、事务日志、时间旅行、compaction | [docs/lab4.md](docs/lab4.md) |

## 快速开始

```bash
# 构建（首选；也提供 CMakeLists.txt）
make -j$(nproc)

# 运行全部 60+ 个测试
make test          # 等价于 ./tdbtest

# 启动 SQL shell
./tiny_duckdb_shell
```

CMake 方式：

```bash
mkdir build && cd build
cmake .. && make -j$(nproc)
./tdbtest
```

Lab 4 是 Python 实验（需要 `pip install duckdb pytest`）：

```bash
cd lab4_lakebase
python3 -m pytest test_lakebase.py -v
python3 demo.py
```

## 学生版 / 参考解答版

本仓库是**参考解答版**（所有任务均已实现）。生成学生骨架版：

```bash
./tools/strip_solutions.sh ../tiny-duckdb-student
```

学生版中每个任务被替换为 `throw NotImplementedException(...)` 桩，代码可以直接编译，测试全部失败，学生按 Lab 顺序逐个解锁。

## 目录结构

```
src/
  include/tiny_duckdb/   头文件
    common/              idx_t、Value、Vector、DataChunk（Lab 0/1 公共层）
    execution/           MorselQueue（Lab 0）、执行引擎（Lab 3）
    storage/             ColumnChunk、RowGroup、TableData、Catalog（Lab 1）
    parser/              PEG 引擎、SQL 文法、AST、Transformer（Lab 2）
    binder/              BoundExpression、Binder（Lab 2）
    planner/             逻辑计划（Lab 2）
    main/                TinyDuckDB、Connection、QueryResult
  */.cpp                 实现（含 [SOLUTION BEGIN Lx.Ty] 标记）
test/                    Lab 0-3 测试（tdbtest 框架，gtest 风格）
lab4_lakebase/           Lab 4：Python + 真实 DuckDB 的湖表实验
docs/                    每个 Lab 的讲义
tools/strip_solutions.sh 生成学生版
third_party/tdbtest/     极简测试框架（避免外部依赖）
```

## 设计取舍（与 DuckDB 的差异）

- **行组大小** 8192 行（DuckDB 为 122880），让多行组行为在小数据量下也可观测；
- **表达式执行** 走 `Value` 抽象而非按类型特化的原始数组循环，牺牲性能换取可读性（Lab 3 有专门练习讨论特化）;
- **NULL 三值逻辑简化**：连接词中 NULL 视为 false，比较遇 NULL 得 NULL；
- **功能集**：单表 + 单 INNER equi-join、5 种聚合、ORDER BY/LIMIT，不支持子查询/外连接/事务。

## 参考

- DuckDB 论文与源码：*DuckDB: an Embeddable Analytical Database* (SIGMOD'19)
- Push-based 执行：*Efficiently Compiling Efficient Query Plans for Modern Hardware* (Tectorwise/Umbra 一脉)
- Morsel-driven 并行：*Morsel-Driven Parallelism* (Leis et al., SIGMOD'14)
- Delta Lake 事务日志：*Delta Lake: High-Performance ACID Table Storage over Cloud Object Stores* (VLDB'20)
