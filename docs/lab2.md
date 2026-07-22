# Lab 2 - SQL 前端：PEG 解析器与 Binder

> 对应代码：`src/parser/sql_grammar.cpp`、`src/parser/transformer.cpp`、`src/binder/binder.cpp`
> 对应测试：`test/lab2_parser_test.cpp`（18 个用例）、`test/lab2_binder_test.cpp`（15 个用例）

## Overview

这个 Lab 实现从 SQL 文本到逻辑计划的完整前端。与 BusTub 直接用 libpg_query 不同，我们**手写**整条链路，因为"SQL 如何变成计划"本身就是值得学的：

```
 "SELECT a + 1 FROM t WHERE a > 0"
      │  SqlParser::Parse        Task L2.T1-T4 (文法) + L2.T5-T7 (Transformer)
      ▼
 AST: SelectStatement
      │  Binder::Bind            Task L2.T8
      ▼
 逻辑计划: LogicalProjection(a+1) → LogicalFilter(a>0) → LogicalGet(t)
```

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| Task L2.T1 | PEG 文法：SELECT 语句骨架（SELECT/FROM/WHERE/GROUP BY/ORDER BY/LIMIT） | 解析器能构建 SelectStmt 树 |
| Task L2.T2 | PEG 文法：表达式优先级链（OR→AND→比较→加减→乘除→一元→原子） | `./tdbtest Lab2ParserTest.ArithmeticPrecedence` 等 |
| Task L2.T3 | PEG 文法：JOIN 子句 | `./tdbtest Lab2ParserTest.JoinOn` |
| Task L2.T4 | PEG 文法：CREATE TABLE / INSERT | `./tdbtest Lab2ParserTest.CreateTable InsertMultipleRows` |
| Task L2.T5 | Transformer：字面量与表达式树 | 表达式相关用例 |
| Task L2.T6 | Transformer：SELECT 语句 | 语句相关用例 |
| Task L2.T7 | Transformer：DDL/DML | `./tdbtest Lab2ParserTest` 全过 |
| Task L2.T8 | Binder：名称解析、类型推导、聚合重写、逻辑计划 | `./tdbtest Lab2BinderTest`（15 个用例）全过 |

**注意**：T1-T4 的文法是一个整体字符串（`sql_grammar.cpp` 中的一个 SOLUTION 块），拆成四步是为了让你**增量开发**——每加一段文法就跑对应的 Transformer/测试，不要试图一次写完整部文法再调试。

## Background

### 我们的 PEG 引擎速查

tiny-duckdb 自带一个 200 行的 packrat 解析器（`src/include/tiny_duckdb/parser/peg.hpp`，**已提供，先读它**）。规则语法：

```
Rule      <- Alternative1 / Alternative2 / Alternative3   # 有序选择：先匹配 1，失败再试 2
Sequence  <- Item1 Item2 Item3                            # 顺序
Optional  <- Item?           Repeat <- Item*   Some <- Item+
Not       <- !Item                                       # 否定先行（不消耗输入）
Literal   <- 'select'                                    # 单引号：大小写不敏感
Raw       <- "select"                                    # 双引号：大小写敏感
```

与正则表达式的最大区别是**有序选择**：`'int' / 'integer'` 永远命中第一个——较长的/更具体的写法必须放前面。每个命中的规则产生一个**解析树节点**：`peg::Ast { name, token, children }`（终结符节点带 token 文本；规则节点带 children）。

### 表达式优先级（T2 的核心）

经典做法是每种优先级一条规则，高层规则"穿透"到低层：

```
OrExpr    <- AndExpr ('or' AndExpr)*
AndExpr   <- ComparisonExpr ('and' ComparisonExpr)*
ComparisonExpr <- AdditiveExpr (ComparisonOp AdditiveExpr)?
AdditiveExpr   <- MultExpr (('+'/'-') MultExpr)*
MultExpr       <- UnaryExpr (('*'/'/') UnaryExpr)*
UnaryExpr      <- '-' UnaryExpr / PrimaryExpr
PrimaryExpr    <- FuncCall / ColumnRef / Literal / '(' OrExpr ')'
```

链条的方向决定一切：`1 + 2 * 3` 中 `*` 在**更低**（更晚归约）的规则里，所以先算——解析树天然是 `add(1, mul(2, 3))`，不需要任何优先级表。这就是"文法编码优先级"。

### Transformer 的三板斧

解析树是通用结构，Transformer 把它匹配成类型化 AST（`parser/ast.hpp`，先浏览一遍类列表）。三个已提供的辅助函数（`TransformComparisonOp` 等）示范了匹配风格；你只会反复用到三个 API：

```cpp
node.Find("WhereClause")      // 第一个名为 WhereClause 的直接子节点，没有则 nullptr
node.FindAll("SelectItem")    // 所有名为 SelectItem 的直接子节点（重复规则用）
node.token                    // 终结符节点的原文
```

## Task #1 - 文法：SELECT 骨架

在 `TinyDuckDBSqlGrammar()` 里实现（对应文法规则已在骨架中列出名字）：

```
Statement      <- SelectStmt / CreateStmt / InsertStmt
SelectStmt     <- 'select' SelectList 'from' TableRef WhereClause? GroupByClause? OrderByClause? LimitClause?
SelectList     <- SelectItem (',' SelectItem)*
SelectItem     <- Star / (Expression Alias?)
WhereClause    <- 'where' Expression
GroupByClause  <- 'group' 'by' Expression (',' Expression)*
OrderByClause  <- 'order' 'by' OrderItem (',' OrderItem)*
OrderItem      <- Expression ('asc' / 'desc')?
LimitClause    <- 'limit' Number
```

**里程碑**：配上临时的空表达式规则（`Expression <- Number`），`ParseTree("SELECT * FROM t")` 应该返回根为 `Statement` 的树；`Lab2ParserTest.ParseTreeShape` 是最早能跑的测试。

## Task #2 - 文法：表达式优先级链

按 Background 的链条实现七条表达式规则。另外两条支柱规则：

```
ColumnRef   <- Identifier ('.' Identifier)?
Literal     <- Number / String / 'true' / 'false' / 'null'
Identifier  <- !Keyword ([a-zA-Z_] [a-zA-Z0-9_]*)
```

**两个经典陷阱**（测试直接命中）：

1. `Identifier` 必须以 `!Keyword` 开头，否则 `select` 本身也能被当成表名；
2. `FuncCall`（如 `count(`）必须排在 `ColumnRef` **之前**，否则 `count(*)` 先被列引用吃掉。

**里程碑**：`ArithmeticPrecedence`、`AndBindsTighterThanOr`、`ParenthesizedArithmetic`、`ConjunctionChainIsLeftAssociative`、`NegativeLiteral`。

## Task #3 - 文法：JOIN 子句

```
TableRef    <- Identifier JoinClause?
JoinClause  <- 'join' Identifier 'on' Expression
```

JOIN 条件复用 Expression——这给了 Transformer 一棵树，Binder（T8）再负责把等值条件拆成左右键。**里程碑**：`JoinOn`。

## Task #4 - 文法：DDL / DML

```
CreateStmt  <- 'create' 'table' Identifier '(' ColumnDef (',' ColumnDef)* ')'
ColumnDef   <- Identifier TypeName
TypeName    <- 'integer' / 'int' / 'bigint' / 'double' / 'real' / 'varchar' / 'text' / 'boolean' / 'bool'
InsertStmt  <- 'insert' 'into' Identifier 'values' RowList
RowList     <- '(' Literal (',' Literal)* ')' (',' '(' Literal (',' Literal)* ')')*
```

**里程碑**：`CreateTable`、`InsertMultipleRows`、`SyntaxErrorThrows`（`blob` 类型必须被拒绝——让它根本不在 TypeName 里）。

## Task #5 - Transformer：字面量与表达式

`TransformLiteral`：Number 无小数点 → `Value::Integer(std::stoi)`，有小数点 → `Value::Double(std::stod)`；String 的 token 带引号，剥掉；`'true'/'false'/'null'` 按 token 文本分派。

`TransformExpression`：按 `node.name` 分派（骨架里已列出全部 case）。三条通用规律：

- **左结合**：`a - b - c` 折叠成 `sub(sub(a, b), c)`——从左到右累加；
- **穿透**：`ComparisonExpr` 只有一个 AdditiveExpr 子节点时直接返回它，不产生比较节点；
- **一元负号**：`-x` 构造为 `OperatorExpression(SUBTRACT, 0, x)`，免去一种新节点类型。

**里程碑**：T2 的全部解析测试 + `ComparisonWithArithmetic`、`StringLiteralWithSpaces`。

## Task #6 - Transformer：SELECT 语句

填充 `SelectStatement` 的每个字段（`parser/ast.hpp` 有完整定义）：

- `SelectItem`：`Star` → `StarExpression`；否则表达式 + 可选 `Alias`（别名放进 `select_aliases`，每项一个，缺省为 `""`——**下标必须与 select_list 对齐**）；
- `TableRef` → `table`；有 `JoinClause` 时置 `has_join/join_table/join_condition`；
- 可选子句一律 `node.Find("WhereClause")`，nullptr 即不存在；
- `OrderItem`：`desc` → `ascending = false`，其余（含缺省）为 true；
- `LimitClause`：`has_limit = true`、`limit = std::stoll(token)`。

**里程碑**：`SelectStar`、`SelectColumnsWithAlias`、`WhereComparison`、`GroupByOrderByLimit`、`MultipleOrderKeys`、`CaseInsensitiveKeywordsAndNames`。

## Task #7 - Transformer：DDL / DML

`TransformCreateTable`：TypeName 小写化后映射（`integer/int→Integer`、`bigint→BigInt`、`double/real→Double`、`varchar/text→Varchar`、`boolean/bool→Boolean`），其余抛 `ParserException`。

`TransformInsert`：每个 Literal 经 `TransformLiteral` 成常量，一行一个 `std::vector<std::unique_ptr<Expression>>`。

至此 `./tdbtest Lab2ParserTest` 应全部通过（18/18）。

## Task #8 - Binder

Binder 把 AST 变成**绑定计划**：名字解析成序号、类型全部推导、结构重排成算子树。`binder/binder.cpp` 里有五个子任务（L2.T8a-e），函数级注释已写明规格，这里是地图：

**T8a `BindExpression`**：按节点类型分派。列引用走 `BindScope::Resolve`（它会替你抛"未知列"和"二义列"——**不要**捕获，测试就靠它）；算术两边必须数值，结果类型 `Value::MaxNumericType(l, r)`，除法恒为 Double；**聚合函数出现在这里直接 BinderException**（比如 WHERE 里写 sum——非法）。

**T8b `BindAggregate`**：`count(*)→COUNT_STAR/BigInt`；`count/sum/avg→BigInt/Double/Double`；`min/max→子表达式类型`；未知函数名 → BinderException。

**T8c `RewriteAfterAggregate`**（本 Lab 的智力高峰）：把 `SELECT flag, sum(q)+1 GROUP BY flag` 的 select 列表改写到聚合**之上**——分组键变成对聚合输出列的引用、聚合函数收进 `aggregates` 列表并换成引用、其余结构（算术、常量）递归重建。与分组键的匹配用**规范化表名+列名**比较（提供的 `Normalize`）。

**T8d `BindSelect`**：自底向上搭计划：

```
LogicalLimit?              （有 LIMIT 时）
 └ LogicalOrder?           （有 ORDER BY 时；键是 (select 输出列号, ascending)）
  └ LogicalProjection      （select 列表）
   └ LogicalAggregate?     （有 GROUP BY 或含聚合函数时）
    └ LogicalFilter?       （有 WHERE 时）
     └ LogicalGet | LogicalJoin
```

JOIN 条件用提供的 `SplitConjunction` 拍平成等式列表，再按"左键只引用左表、右键只引用右表"配对，否则 BinderException（本系统只支持 INNER equi-join）。

**T8e `BindInsert`**：行宽对齐列数；字面量向列类型强制（数值→数值用 `GetNumeric` 宽化、字符串→VARCHAR 原样、`'null'`→带类型 NULL、其余 BinderException）。

**里程碑**：`./tdbtest Lab2BinderTest` 15 个用例全过。

## Testing

```bash
./tdbtest Lab2                      # 33 个用例全跑
./tdbtest Lab2ParserTest            # 只跑解析器 18 个
./tdbtest Lab2BinderTest.Bind       # 按前缀跑 binder 的一组
```

## Development Hints

- **增量开发文法**：每加一两条规则立刻编译跑测试。PEG 的错误信息（"unexpected ... at offset N"）指向原文偏移，比大多数 LR 生成器友好，但一次写 30 条规则再调仍然是噩梦。
- `SqlParser::ParseTree(sql)` 可把任何语句的原始解析树打印出来（每个节点 name+token）——Transformer 对不上树形时先看它。
- Binder 出错时用排除法：`BindScope::Resolve` 的异常消息会写明是 unknown 还是 ambiguous。

## Grading Rubric

1. `./tdbtest Lab2` 33 个用例全部通过；
2. 编译零警告；
3. 文法不得引入正则库或其他第三方解析库（本 Lab 的目的就是手写）；
4. Binder 不得"特判测试"（如按表名硬编码行为）。

## 思考题（不计分）

- PEG 的有序选择天然消除了经典文法的 dangling-else 二义性。想想为什么。
- 我们的 `count(*)` 是文法层特判（FuncCall 里允许 `*`）。在 PostgreSQL 里它走的是另一条路（把一个伪列喂给聚合）。两种设计的取舍？
- 为什么 Binder 要把"聚合重写"放在逻辑计划层而不是交给执行器判断？（提示：想想聚合算子的输出 schema 固定后，上面的投影如何保持简单。）
