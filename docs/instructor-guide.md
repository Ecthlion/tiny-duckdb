# tiny-duckdb 授课与发布指南

这份文档面向课程维护者。学生从各 Lab 讲义开始；教师用本页安排节奏、发布学生骨架、维护参考解答和组织评分。

## 1. 与 BusTub 的对齐原则

tiny-duckdb 借鉴的是 BusTub 的教学工程方法，而不是照搬其 OLTP 组件：

- 一个可运行的数据库骨架贯穿全课，任务之间有明确依赖；
- 每个任务同时给出接口契约、里程碑测试、开发提示和评分标准；
- Debug 构建默认启用 sanitizer，格式检查进入 CI；
- 学生可以按测试套件或单个用例迭代，教师保留额外边界测试；
- 参考解答与学生骨架严格隔离。

对照资料：

- [BusTub 仓库与环境说明](https://github.com/cmu-db/bustub)
- [Fall 2025 Project 0](https://15445.courses.cs.cmu.edu/fall2025/project0/)
- [Fall 2025 Project 1](https://15445.courses.cs.cmu.edu/fall2025/project1/)
- [Fall 2025 Project 3](https://15445.courses.cs.cmu.edu/fall2025/project3/)

## 2. 建议课时

| Lab | 建议时间 | 教学主线 | 进入条件 |
|-----|----------|----------|----------|
| 0 | 0.5 周 | 原子 read-modify-write、morsel 调度 | C++17 基础 |
| 1 | 1.5 周 | 列存、行组、zone map、并发 append | Lab 0 |
| 2 | 2 周 | PEG、AST、Binder、逻辑计划 | Lab 1 可并行进行 |
| 3 | 3 周 | push pipeline、向量化、并行聚合、join | Lab 1 + 2 |
| 4 | 1 周 | Parquet 湖表、日志、时间旅行、compaction | Python 基础 |
| 5 | 1 周 | embedding 距离表达式、exact Top‑K | Lab 1-3 |

Lab 3 是课程主项目，建议在 T1-T3 和 T4-T6 之间安排一次 checkpoint。Lab 4 可作为开放实验，不阻塞 C++ 主线。

## 3. answer / student 发布模型

仓库使用 `.tiny-duckdb-edition` 标识版本：

- `answer`：单一真源，任务实现放在成对的 `[SOLUTION BEGIN ...]` / `[SOLUTION END]` 标记内；
- `student`：由 `answer` 生成，标记块被替换为可编译的未实现桩。

推荐发布顺序：

```bash
# 在 answer 分支
cmake -S . -B cmake-build -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build -j
ctest --test-dir cmake-build --output-on-failure

python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r lab4_lakebase/requirements.txt
python -m pytest lab4_lakebase/test_lakebase.py -v

# 生成并构建学生版
bash tools/strip_solutions.sh ../tiny-duckdb-student
cmake -S ../tiny-duckdb-student -B ../tiny-duckdb-student/cmake-build \
  -DCMAKE_BUILD_TYPE=Debug -DTINY_DUCKDB_SANITIZER=
cmake --build ../tiny-duckdb-student/cmake-build -j
```

CI 不再依赖分支名，而是读取 edition 文件：answer 必须通过全部测试，并额外验证生成的 student 能编译；student 必须不含解答标记。

如果课程用于正式评分，不要把 `answer` 推到学生可见的公开远端。BusTub 也明确要求解答使用私有仓库；双分支在同一公开仓库里并不能形成访问隔离。

## 4. 测试与评分建议

建议每个 Lab 分三层：

1. 公开里程碑测试：覆盖正常路径，让学生能迭代；
2. 教师边界测试：覆盖空输入、NULL、chunk/row-group 边界、并发和错误类型；
3. 集成测试：用 SQL 从 Parser 一直跑到结果，防止只为局部测试硬编码。

总分可按“正确性 80% + sanitizer 10% + 格式与代码质量 10%”。接口签名、测试与 provided helpers 不允许修改；允许学生增加私有 helper 和状态字段。对性能机制不能只检查答案，还应检查可观测指标或基准结果。

当前 C++ 测试可这样运行：

```bash
./tdbtest --list
./tdbtest Lab1
./tdbtest Lab3ExecutionTest.Join
ctest --test-dir cmake-build -L lab5 --output-on-failure
```

## 5. 下一阶段优化路线

### P0：发布可靠性

- 把 `answer` 放入私有教师远端，公开远端只发布 student；
- 为仓库选择并补充 LICENSE；
- 固定 clang-format 主版本，避免 Ubuntu 与 Homebrew 最新版格式结果漂移；
- 将每次发布的 starter commit/tag 写进讲义，便于学生同步补丁。

### P1：让性能优化可观测

- 给 TableScan 增加 `morsels_total / morsels_pruned / rows_scanned` 统计，再让 zone map 测试断言确实跳读；
- 给 shell 增加 `.explain` 或 SQL `EXPLAIN`，展示逻辑计划、物理算子和 pipeline；
- 引入一个轻量 SQLLogicTest runner，把 Lab 3 的端到端 SQL 从 C++ 测试中抽成可读用例；
- 给 Lab 3 增加基准目标，量化线程数、vector size、局部聚合和 join fanout。

### P2：扩展 AP 主线

- 把 `ORDER BY + LIMIT` 融合为 Top‑N，作为 exact vector retrieval 的自然优化；
- 增加字典编码/RLE 或 filter pushdown 任务，让“列存为何快”有可测性能差异；
- 增加简单 optimizer rule（projection/filter pushdown、Top‑N rewrite），对齐 BusTub Project 3 的规则优化训练；
- 将 Lab 2 的单块 PEG 和 `L2.T8` 拆成真正独立的 checkpoint/标记，减少全有或全无的调试；
- 为 Lab 4 增加并发提交冲突测试；当前 rename 只保证单文件替换原子可见，并不能独自解决两个 writer 抢同一 version 的问题。

其中最值得先做的是“扫描统计 + EXPLAIN + SQLLogicTest”。这三项能让学生从“测试过了”升级到“看得见系统为何这样执行”。
