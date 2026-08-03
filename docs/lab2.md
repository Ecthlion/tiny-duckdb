# Lab 2 - SQL 前端：PEG 解析器 → AST → Binder → 逻辑计划

> 对应代码：[sql_grammar.cpp](file:///Users/bytedance/Projects/tiny-duckdb/src/parser/sql_grammar.cpp)、[transformer.cpp](file:///Users/bytedance/Projects/tiny-duckdb/src/parser/transformer.cpp)、[binder.cpp](file:///Users/bytedance/Projects/tiny-duckdb/src/binder/binder.cpp)
> PEG 引擎（已提供）：[peg.hpp](file:///Users/bytedance/Projects/tiny-duckdb/src/include/tiny_duckdb/parser/peg.hpp)
> AST 定义：[ast.hpp](file:///Users/bytedance/Projects/tiny-duckdb/src/include/tiny_duckdb/parser/ast.hpp)
> 对应测试：[lab2_parser_test.cpp](file:///Users/bytedance/Projects/tiny-duckdb/test/lab2_parser_test.cpp)（18 个）、[lab2_binder_test.cpp](file:///Users/bytedance/Projects/tiny-duckdb/test/lab2_binder_test.cpp)（15 个）

## Overview

BusTub 直接用了 libpg_query（PostgreSQL 的词法/语法分析器），学生看不到"SQL 怎么变成计划"。tiny-duckdb 让你**手写整条前端链路**——从 PEG 文法到类型推导，你会真正理解一条 SQL 的旅程。

```
┌──────────────────────────────────────────────────────────────────────────┐
│                        SQL 前端管线（本 Lab）                            │
│                                                                          │
│  "SELECT a+1 AS r FROM t WHERE a>0 GROUP BY r ORDER BY r LIMIT 10"      │
│       │                                                                  │
│       │ ① Parser（PEG 文法，T1-T4）                                     │
│       ▼                                                                  │
│  ┌─────────────────────────────────────────┐                             │
│  │  解析树 peg::Ast（通用树结构）           │                             │
│  │  Statement→SelectStmt→SelectList...    │                             │
│  │  每个节点: {name, token, children[]}    │                             │
│  └─────────────────────────────────────────┘                             │
│       │                                                                  │
│       │ ② Transformer（AST 构建，T5-T7）                                │
│       ▼                                                                  │
│  ┌─────────────────────────────────────────┐                             │
│  │  类型化 AST（SelectStatement 等）       │                             │
│  │  expression 是类树，token 变成 Value    │                             │
│  └─────────────────────────────────────────┘                             │
│       │                                                                  │
│       │ ③ Binder（绑定 + 重写 + 计划构建，T8）                          │
│       ▼                                                                  │
│  ┌─────────────────────────────────────────┐                             │
│  │  逻辑计划树（LogicalOperator 树）       │                             │
│  │  列名→列号、类型推导完成、聚合被重写    │                             │
│  └─────────────────────────────────────────┘                             │
│       │                                                                  │
│       │ Planner/Optimizer（已提供）                                      │
│       ▼                                                                  │
│  物理计划 ──► Lab 3 的执行引擎                                           │
└──────────────────────────────────────────────────────────────────────────┘
```

| 任务 | 内容 | 通过标准 |
|------|------|----------|
| Task L2.T1 | PEG 文法：SELECT 骨架 | `ParseTreeShape` 等基础用例 |
| Task L2.T2 | PEG 文法：表达式优先级链 | `ArithmeticPrecedence` 等 |
| Task L2.T3 | PEG 文法：JOIN 子句 | `JoinOn` |
| Task L2.T4 | PEG 文法：CREATE TABLE / INSERT | `CreateTable` `InsertMultipleRows` |
| Task L2.T5 | Transformer：字面量与表达式树 | 表达式相关用例 |
| Task L2.T6 | Transformer：SELECT 语句 | `SelectStar` `WhereComparison` 等 |
| Task L2.T7 | Transformer：DDL/DML | 解析器测试全过（18/18） |
| Task L2.T8 | Binder：名称解析、类型推导、聚合重写、逻辑计划 | Binder 测试全过（15/15） |

**重要**：T1-T4 的文法是一个整体字符串（`sql_grammar.cpp` 中的一个 `SOLUTION BEGIN/END` 块）。拆成四步是为了让你**增量开发**——每加一段文法就编译跑对应的测试，不要试图一次写完整部文法再调试。

## Background

### PEG（Parsing Expression Grammar）速成

tiny-duckdb 自带一个 200 行的 packrat 解析器（[peg.hpp](file:///Users/bytedance/Projects/tiny-duckdb/src/include/tiny_duckdb/parser/peg.hpp)），你写文法字符串，它编译成递归下降解析器，产生通用的 `peg::Ast` 树。

**规则语法**：

```
Rule         <- Alt1 / Alt2 / Alt3     # 有序选择：先匹配 Alt1，失败再试 Alt2
Sequence     <- Item1 Item2 Item3       # 顺序匹配
Optional     <- Item?                   # 0 或 1 次
Repeat       <- Item*                   # 0 或多次
Some         <- Item+                   # 1 或多次
Not          <- !Item                   # 否定先行（不消耗输入）
LiteralCI    <- 'select'                # 单引号：大小写不敏感关键字
LiteralCS    <- "exact"                 # 双引号：大小写敏感
CharClass    <- [a-zA-Z_]               # 字符类（如 ['] 匹配单引号——本引擎不支持 \' 转义）
NegatedClass <- [^0-9]                  # 否定字符类
```

**PEG 和正则/BNF 的关键区别**：
1. **有序选择 `/`**：按顺序尝试，第一个匹配的赢。这是 PEG 最核心的特性，也是最容易踩坑的地方——见下方"前缀匹配陷阱"。
2. **无歧义**：有序选择消除了 shift/reduce conflict，不需要优先级声明或 yacc/bison 那种技巧。
3. **空白自动跳过**：序列元素之间的空白自动被吃掉；但字符类前不跳过（所以标识符规则能正常工作）。

**⚠️ 前缀匹配陷阱：两种情况，两种结论**

PEG 的有序选择里，如果一个选项是另一个的前缀，顺序就很重要。但这里要区分两类字面量：

| 字面量类型 | 例子 | 边界检查 | 前缀会误匹配吗？ | 顺序要求 |
|-----------|------|---------|----------------|---------|
| **标识符/关键字**（末尾是字母/数字/下划线） | `'int'` vs `'integer'` | ✅ 有（peg.cpp 检查后跟字符是否也是标识符字符） | ❌ `'int'` 不会匹配 `integer` | 任意顺序都行（长的前置于可读性） |
| **运算符/标点**（末尾是非标识符字符） | `'<'` vs `'<='` | ❌ 无 | ✅ `'<'` 会抢吃 `<=` 的前缀 | **长的必须在前** |

所以：`CmpOp` 里 `'<='` 必须在 `'<'` 前面；但 `TypeName` 里 `'int'` 和 `'integer'` 的顺序无关紧要（引擎帮你防了）。这是为什么同一个 PEG 引擎里，CmpOp 顺序致命但 TypeName 顺序不致命。

每个命中的规则产生一个 Ast 节点：

```cpp
struct Ast {
    std::string name;                     // 规则名（如 "SelectStmt"、"Number"）
    std::string token;                    // 终结符匹配到的原文
    std::vector<unique_ptr<Ast>> children; // 子规则的 Ast
};
```

Transformer 三个 API 你会用无数次：

```cpp
node.Find("WhereClause")     // 第一个名为 WhereClause 的直接子节点，没有则 nullptr
node.FindAll("SelectItem")   // 所有名为 SelectItem 的直接子节点
node.token                   // 终结符节点的原始文本
```

### 表达式优先级——用文法结构编码优先级（T2 的核心）

表达式优先级不需要优先级表——**把优先级写进文法递归层次里**：

```
OrExpr        <- AndExpr ('or' AndExpr)*           最低优先级（最先归约）
AndExpr       <- CmpExpr ('and' CmpExpr)*
CmpExpr       <- AddExpr (CmpOp AddExpr)?
AddExpr       <- MulExpr (('+'/'-') MulExpr)*
MulExpr       <- UnaryExpr (('*'/'/') UnaryExpr)*
UnaryExpr     <- '-' UnaryExpr / PrimaryExpr
PrimaryExpr   <- '(' Expression ')' / FuncCall / Literal / ColumnRef
                                                                      最高优先级
Literal       <- Number / String / 'true' / 'false' / 'null'
ColumnRef     <- Identifier ('.' Identifier)?
Identifier    <- !Keyword [a-zA-Z_] [a-zA-Z0-9_]*     ← !Keyword 防止关键字当列名
FuncCall      <- Identifier '(' (Star / Expression (',' Expression)*)? ')'
```

> **注意可选参数与分组**：`FuncCall` 的参数表整体是可选的（`?`），所以 `f()`、`f(1)`、`count(*)` 都合法。`Star / Expression ...` 是参数位置的顶层二选一——PEG 序列里 `/` 的优先级最低，`Star / Expression (',' Expression)*` 会被正确理解为"要么 `*`，要么一个表达式列表"，不需要额外括号。

**方向决定优先级**：高层规则"穿透"到低层规则，低层规则先匹配。

```
1 + 2 * 3 的解析过程：
  OrExpr → AndExpr → CmpExpr → AddExpr
    AddExpr 先匹配到 1，然后看到 +，右边必须是 MulExpr
    MulExpr 匹配到 2*3（* 在 MulExpr 层），返回 mul(2,3)
  结果：add(1, mul(2, 3))    ← 正确！* 先算
```

**两个经典陷阱**：
1. `Identifier` 必须以 `!Keyword` 开头，否则 `select from where` 里的关键字会被当成列名
2. `FuncCall` 必须排在 `ColumnRef` **之前**，否则 `count(*)` 会被 `ColumnRef` 先吃掉 `count`，留下的 `(*)` 报语法错

### Binder：从 AST 到逻辑计划（T8）

Binder 做四件事：**名字解析**（列名→列号）、**类型推导**、**聚合重写**、**计划树构建**。这是本 Lab 的智力高峰。

#### 计划的构建顺序（T8d）

逻辑计划自底向上搭，每一层包一层：

```
LogicalLimit?               ← 有 LIMIT 时在最外层
 └ LogicalOrder?            ← 有 ORDER BY
  └ LogicalProjection       ← SELECT 列表（总是有）
   └ LogicalAggregate?      ← 有 GROUP BY 或聚合函数时
    └ LogicalFilter?        ← 有 WHERE 时
     └ LogicalGet | LogicalJoin   ← 叶子：扫表或 JOIN
```

```
示例：SELECT name, sum(score)+1 FROM t WHERE id>10 GROUP BY name ORDER BY 2 LIMIT 5

LogicalLimit(5)
 └ LogicalOrder([{col=1, asc=true}])
   └ LogicalProjection([name, sum_score+1])
     └ LogicalAggregate(group_by=[name], aggs=[sum(score)])
       └ LogicalFilter(id>10)
         └ LogicalGet(t)
```

#### 聚合重写（T8c）——本 Lab 最难的部分

SQL 语义允许你在 SELECT 列表里写 `sum(score)+1`，但执行引擎的聚合算子只能输出 `[group_key, aggregate_result...]`。所以 Binder 必须**重写**：

```
重写前（用户写的）：
  SELECT name, sum(score)+1 FROM t GROUP BY name
  Projection: [name, sum(score)+1]

重写后（引擎执行的）：
  Aggregate(group_by=[name], aggs=[sum(score)])    ← 聚合算子只做聚合
    └ output: [name, sum(score)]
  Projection: [ref(name), ref(sum_score)+1]         ← 投影做后续算术
```

规则：
- GROUP BY 里的列引用 → 变成对聚合输出的列引用（按分组键的序号）
- 聚合函数（sum/count/...）→ 移到 aggs 列表，原位置变成对聚合输出的引用
- 其他结构（算术运算、常量）→ 递归重建，但内部的聚合和列引用被替换
- WHERE 里的聚合函数 → **报错**（SQL 语义不允许在 WHERE 里用聚合，要用 HAVING；我们简化为直接拒绝）

#### JOIN 处理

SQL 里的 `t1 JOIN t2 ON t1.a = t2.b AND t1.c > 10` 不是所有条件都是等值连接键。Binder 要：
1. 把 ON 条件用 `SplitConjunction` 拍平成 AND 列表
2. 找出"左键只引用左表、右键只引用右表"的**等式** → 这些是 hash join 的键
3. 其余条件 → 留作 join 之后的 Filter（或直接报错，如果不是 inner equi-join）
4. 我们只支持 INNER EQUI-JOIN，其他一律 BinderException

## Task #1 - 文法：SELECT 骨架

在 `TinyDuckDBSqlGrammar()` 里实现以下规则（骨架文件中已列出规则名）：

```
Statement      <- SelectStmt / CreateTableStmt / InsertStmt
SelectStmt     <- 'select' SelectList FromClause WhereClause? GroupByClause?
                                         OrderByClause? LimitClause?
SelectList     <- SelectItem (',' SelectItem)*
SelectItem     <- Expression Alias? / Star
Alias          <- 'as'? Identifier         /* 'as' 可省略：SELECT a b  = SELECT a AS b */
FromClause     <- 'from' TableRef
TableRef       <- Identifier               /* JOIN 在 T3 扩展 */
WhereClause    <- 'where' Expression
GroupByClause  <- 'group' 'by' Expression (',' Expression)*
OrderByClause  <- 'order' 'by' OrderItem (',' OrderItem)*
OrderItem      <- Expression ('asc' / 'desc')?
LimitClause    <- 'limit' Number
Expression     <- OrExpr                    /* 表达式入口，T2 实现完整链 */
Star           <- '*'
```

**里程碑**：先写临时桩 `OrExpr <- Number`（这样 Expression 就通了），`SELECT * FROM t` 和 `SELECT a FROM t WHERE 1` 应该能解析。跑 `ParseTreeShape` 验证树结构。

## Task #2 - 文法：表达式优先级链

按 Background 的优先级层次实现七条表达式规则（OrExpr 到 PrimaryExpr），以及：

```
ColumnRef      <- Identifier ('.' Identifier)?     /* t.a 或 a */
Literal        <- Number / String / 'true' / 'false' / 'null'
Identifier     <- !Keyword ([a-zA-Z_] [a-zA-Z0-9_]*)
FuncCall       <- Identifier '(' (Star / Expression (',' Expression)*)? ')'
CmpOp          <- '!=' / '<>' / '<=' / '>=' / '=' / '<' / '>'
Number         <- [0-9]+ ('.' [0-9]+)?              /* 负号由 UnaryExpr 的 '-' UnaryExpr 处理，不要在这里加 */
String         <- ['] (!['] .)* [']                /* 匹配单引号字符串：[^'] 不含反斜杠转义，用 not-predicate */
```

**⚠️ CmpOp 顺序是 PEG 的经典陷阱**：对于 `<` / `<=` 这种**前缀关系**，**长的必须放前面**。与关键字不同（`'int'` 不会匹配 `integer` 前缀，因为 PEG 引擎在 `peg.cpp` 做了标识符边界检查），运算符是**非标识符字符**，边界检查不生效。如果 `'<'` 在 `'<='` 前面，输入 `a <= b` 时 `<` 会先成功匹配（吃掉 `<`），然后 AdditiveExpr 从 `= b` 开始匹配失败，整个比较子句回退成"无比较"，`= b` 被当作后续子句导致语法错。

**易错点提醒**：
- `FuncCall` 放在 `ColumnRef` 前面（否则 `count(*)` 的 `count` 被 ColumnRef 吃掉）
- `!Keyword` 断言必须在 Identifier 最前面
- 字符串里的单引号用字符类 `[']` 匹配——本引擎的 PEG 字面量**不支持反斜杠转义**，所以写 `'\''` 会错误匹配一个反斜杠而不是单引号
- 比较运算符严格按"双字符在前、单字符在后"排列：`<=`/`>=`/`!=`/`<>` → `=` → `<` → `>`

**里程碑**：`ArithmeticPrecedence`、`AndBindsTighterThanOr`、`ParenthesizedArithmetic`、`ConjunctionChainIsLeftAssociative`、`NegativeLiteral`。

## Task #3 - 文法：JOIN

```
TableRef       <- Identifier JoinClause?
JoinClause     <- 'inner'? 'join' Identifier 'on' Expression
```

JOIN 条件复用 Expression——Transformer 拿到完整表达式树，Binder（T8）负责拆分等值键。

**里程碑**：`JoinOn`。

## Task #4 - 文法：DDL / DML

```
CreateTableStmt <- 'create' 'table' Identifier '(' ColumnDef (',' ColumnDef)* ')'
ColumnDef      <- Identifier TypeName
TypeName       <- 'integer' / 'int' / 'bigint' / 'double' / 'real'
                 / 'varchar' / 'text' / 'boolean' / 'bool'
InsertStmt     <- 'insert' 'into' Identifier 'values' RowList
RowList        <- Row (',' Row)*
Row            <- '(' Literal (',' Literal)* ')'
```

**注意**：TypeName 的顺序。你可能会担心 `'int'` 抢在 `'integer'` 前面匹配掉前缀——放心，这**不会**发生：PEG 引擎（`peg.cpp` 关键字边界检查）会拒绝"字面量末尾是标识符字符、且紧跟另一个标识符字符"的匹配，所以 `'int'` 不会吃 `integer`。但为了可读性，仍然建议长的在前：`'integer' / 'int'`。（对比 T2 的 CmpOp：运算符**不是**标识符字符，所以边界检查不生效，`<` 真的会抢 `<=` 的前缀——这是两种不同的情况。）

**里程碑**：`CreateTable`、`InsertMultipleRows`、`SyntaxErrorThrows`（`blob` 等不在 TypeName 里的类型应抛 ParserException）。

## Task #5 - Transformer：字面量与表达式

`TransformLiteral`：
- Number token 含 `.` → `Value::Double(std::stod(token))`
- Number token 不含 `.` → `Value::Integer(std::stoi(token))`（进阶：用 `std::stoll` 并检查 `>INT32_MAX` 时返回 `Value::BigInt`，避免溢出）
- String token：子节点是 `String` 规则匹配的（带首尾引号），剥掉首尾单引号，`Value::Varchar(内容)`
- `'true'`/`'false'` → `Value::Boolean(true/false)`
- `'null'` → `Value::Null(LogicalType::Integer())`（Null 需要一个类型参数，给 Integer 即可，后续 Binder 会做类型提升）

`TransformExpression`：按 `node.name` 分派，三个关键模式：
- **左结合二元运算**：`AddExpr`/`MulExpr` 的 children 交替为 `operand op operand op ...`，从左到右折叠：`a-b-c → sub(sub(a,b),c)`
- **穿透规则**：`CmpExpr` 只有一个子节点（没有 CmpOp）时，直接穿透返回那个子节点的 Transform 结果（不产生比较节点）；有 CmpOp 时 children[0]=左、Find("CmpOp")=运算符、children[2]=右
- **一元负号**：`UnaryExpr` 的第一个子节点也是 `UnaryExpr` 时就是 `-x`（即匹配了 `- UnaryExpr` 分支），构造为 `OperatorExpression(SUBTRACT, Value::Integer(0), x)`；否则（匹配了 PrimaryExpr 分支）直接穿透返回子节点结果

## Task #6 - Transformer：SELECT 语句

填充 `SelectStatement` 结构体的每个字段（定义在 [ast.hpp](file:///Users/bytedance/Projects/tiny-duckdb/src/include/tiny_duckdb/parser/ast.hpp)）：

- **SelectList**：遍历 `FindAll("SelectItem")`
  - 子节点是 `Star` → 添加 `StarExpression`
  - 否则：一个 Expression + 可选 Alias（`Find("Alias")`，有则 `Find("Identifier")->token` 作别名；无则空串）
  - **注意**：`select_aliases` 向量下标必须和 `select_list` 对齐
- **TableRef**：`Find("TableRef")->Find("Identifier")->token`
- **JoinClause**：`Find("JoinClause")` 存在时设置 `has_join=true`、`join_table`、`join_condition`
- **WhereClause**：`Find("WhereClause")`，nullptr 则无
- **GroupBy/OrderBy/Limit**：类似处理，OrderItem 里 `Find("desc")` 存在则 `ascending=false`

## Task #7 - Transformer：DDL/DML

`TransformCreateTable`：
- TypeName token 小写化后映射到 LogicalType（`integer/int→Integer`，`bigint→BigInt`，`double/real→Double`，`varchar/text→Varchar`，`boolean/bool→Boolean`）
- 未识别类型抛 ParserException

`TransformInsert`：每行的 Literal 列表 Transform 成 `vector<unique_ptr<Expression>>`（常量表达式）。

至此 `./tdbtest Lab2ParserTest` 应全部 18/18 通过。

## Task #8 - Binder

Binder 是最复杂的任务，函数级注释在 [binder.cpp](file:///Users/bytedance/Projects/tiny-duckdb/src/binder/binder.cpp) 里已经很详细。这里给你五个子任务的地图：

### T8a `BindExpression`

按表达式类型分派：
- **列引用**（`ColumnRefExpression`）：走 `BindScope::Resolve`，它会抛"unknown column"或"ambiguous column"——不要捕获，测试就靠这个
- **常量**（`ConstantExpression`）：直接返回（类型已是常量类型）
- **算术运算**（`OperatorExpression`）：递归 bind 左右子节点，检查两边都是数值类型，结果类型用 `Value::MaxNumericType(l, r)`；除法恒为 Double
- **比较/连接词**：递归 bind 子节点，返回类型 Boolean
- **聚合函数**出现在这里 → **抛 BinderException**（聚合不能出现在 WHERE 或 GROUP BY 里，应该在 T8c 被处理）
- **STAR**：`SELECT *` 走特殊路径（展开为所有列）

### T8b `BindAggregate`

| SQL 函数 | 聚合类型 | 返回类型 | 注意 |
|----------|---------|---------|------|
| `count(*)` | COUNT_STAR | BigInt | 不检查子表达式 |
| `count(col)` | COUNT | BigInt | NULL 跳过 |
| `sum(col)` | SUM | 整数→BigInt, 浮点→Double | NULL 跳过 |
| `avg(col)` | AVG | Double | NULL 跳过 |
| `min(col)`/`max(col)` | MIN/MAX | 子表达式类型 | NULL 跳过 |

未知函数名 → BinderException。

### T8c `RewriteAfterAggregate`——核心智力点

输入：原始 SELECT 列表表达式 + 分组键信息
输出：重写后的 SELECT 列表 + aggregates 列表

对每个 select 表达式递归：
1. 遇到**聚合函数调用**（count/sum/avg/min/max）：bind 它的子参数，加入 `aggregates` 列表，返回一个**列引用**指向 aggregates 输出（位置是 aggregates.size()-1）
2. 遇到**列引用**：检查它是否匹配某个 GROUP BY 键（用 `Normalize` 规范化表名+列名比较），如果匹配则返回指向对应 group by 输出位置的列引用；不匹配则抛 BinderException（"column must appear in GROUP BY or aggregate"）
3. 遇到**算术/比较**：递归重写子节点，重建同类型表达式
4. 遇到**常量**：直接返回

### T8d `BindSelect`——组装计划树

按 Background 里的层次自底向上搭：
1. 处理 FROM → 创建 LogicalGet（如果有 JOIN 则先处理 join 子树）
2. 处理 WHERE → 包 LogicalFilter
3. 处理 GROUP BY 或聚合 → 调 RewriteAfterAggregate，包 LogicalAggregate
4. 处理 SELECT 列表 → 包 LogicalProjection
5. 处理 ORDER BY → 包 LogicalOrder
6. 处理 LIMIT → 包 LogicalLimit

JOIN 条件处理：
1. 调 `SplitConjunction(join_condition)` 拍平成 AND 列表
2. 找等值条件（`l.col = r.col`），验证左键只引用左表、右键只引用右表
3. 找到的第一个等值条件作为 join 键；如果有其他非等值条件，BinderException（简化：不支持 non-equi join）
4. 如果没有等值条件，BinderException

### T8e `BindInsert`

- 行数必须匹配列数
- 每个值向列类型强制转换：
  - 数值→数值：宽化（Integer→BigInt→Double）
  - 字符串→Varchar：原样
  - NULL：带列类型的 NULL
  - 类型不匹配：BinderException

**里程碑**：`./tdbtest Lab2BinderTest` 15/15 全过。

## Testing

```bash
make -j4
./tdbtest Lab2                       # 33 个用例全跑（18 解析 + 15 绑定）
./tdbtest Lab2ParserTest             # 只跑解析器
./tdbtest Lab2BinderTest             # 只跑 Binder
./tdbtest Lab2ParserTest.Arithmetic  # 按前缀过滤
```

调试技巧：
- **先打印解析树**：`SqlParser::ParseTree(sql)->ToString()` 打印完整树结构，Transformer 对不上形状时先看它
- Binder 出错时用排除法：异常消息会写明是 unknown column、ambiguous、还是 aggregate in WHERE

## Development Hints

- **增量开发文法**：每加 2-3 条规则立刻编译跑测试。PEG 一次写 30 条规则再调是噩梦——错误定位会非常痛苦
- 文法里的关键字用单引号（大小写不敏感），标识符/字符串内容用双引号或字符类
- Transformer 是纯机械转换：Ast → 类型化表达式，没有业务逻辑
- Binder 的 T8c 聚合重写建议单独在纸上画一个例子（`SELECT name, sum(score)+1 GROUP BY name`），再动手写代码
- 不要尝试在 Binder 里"特判测试"——用通用的名字解析和类型推导逻辑，测试只是验证你的逻辑是否正确

## Grading Rubric

1. `./tdbtest Lab2` 33 个用例全部通过；
2. 编译零警告；
3. 不得引入第三方解析库（本 Lab 的目的就是手写前端）；
4. Binder 不按表名/列名硬编码行为；
5. 类型检查必须正确（字符串不能加减、聚合不能出现在 WHERE）。

## 往下看

- **Lab 3（执行引擎）**：把你在 Lab2 产出的逻辑计划变成物理 Pipeline，用 Lab0 的 MorselQueue 驱动多线程执行，从 Lab1 的存储层里拉数据

## 思考题（不计分）

1. PEG 的有序选择 `/` 天然消除了经典 yacc/bison 的 dangling-else 二义性。想想为什么。（提示：if E then S if E then S else S——PEG 总是先尝试哪种匹配？）
2. 我们的 `count(*)` 是文法层特判（FuncCall 里允许 Star）。PostgreSQL 是把 `*` 当作一个特殊的表达式节点（`A_Star`）传给聚合。两种设计的取舍是什么？
3. 为什么聚合重写要在**逻辑计划层**（Binder 输出）做，而不是在执行器里判断？（提示：想想聚合算子输出 schema 固定后，上层 Projection 的 schema 如何推导。）
4. 为什么我们不支持 HAVING 子句？它在执行模型里应该处于什么位置？（提示：WHERE 过滤原始行，HAVING 过滤聚合后的行——它应该是 Aggregate 之上的一个 Filter。）
