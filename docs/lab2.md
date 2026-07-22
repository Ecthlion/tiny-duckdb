# Lab 2 - SQL 前端：PEG 解析器、Transformer、Binder

> 对应代码：`src/parser/peg.cpp`、`src/parser/sql_grammar.cpp`、`src/parser/transformer.cpp`、`src/binder/binder.cpp`
> 测试：`test/lab2_parser_test.cpp`、`test/lab2_binder_test.cpp`

## 背景：从 SQL 文本到逻辑计划

```
"SELECT a FROM t WHERE a > 1"
        │  Parser (PEG)          语法：字符串 → 解析树
        ▼
   peg::Ast  (规则名 + token + 子节点)
        │  Transformer           语法 → 语义 AST
        ▼
   SelectStatement (ColumnRefExpression 等，名字未解析)
        │  Binder                名字 → 列号，类型检查，逻辑计划
        ▼
   BoundStatement (LogicalProjection → LogicalFilter → LogicalGet)
```

## PEG 与我们的迷你引擎（L2.T1 - T4）

PEG（Parsing Expression Grammar）和传统 CFG（LR/LL）有两点本质不同：

1. **有序选择**：`A / B` 先试 A，成功就不再试 B——没有歧义，文法即程序；
2. **可组合谓词**：`&e`（向前看，必须匹配但不消费）、`!e`（必须不匹配）。

由于不引入第三方库，tiny-duckdb 自带一个 ~450 行的 packrat（记忆化）PEG 引擎，支持：

```
'e'   字面量（大小写不敏感，带关键字边界）   [a-z]  字符类    .     任意字符
e1 e2 序列        e1 / e2  有序选择            e?    可选
e*    零次或多次  e+        一次或多次         &e/!e 谓词
RuleName          规则引用（构成解析树的节点）
```

packrat 的要点：`(规则, 输入位置) → 结果` 缓存，把指数回溯摊成线性。SQL 文法（`sql_grammar.cpp`）里最关键的是**表达式优先级链**：

```
Expression → OrExpr → AndExpr → ComparisonExpr → AdditiveExpr → MultExpr → UnaryExpr → PrimaryExpr
```

每一层都是 `X ← Y (OP Y)*` 的左结合展开，于是 `1 + 2 * 3` 自然是 `add(1, mul(2,3))`，`a=1 OR b=2 AND c=3` 自然是 `OR(a=1, AND(b=2,c=3))`。

另一个教学要点是**空白跳过的规则**：标识符 `Identifier ← !Keyword [a-zA-Z_] [a-zA-Z0-9_]*` 内部绝不能跳过空白（否则 `a from t` 会被吸成一个标识符），而语法层面的元素之间可以。引擎里用"序列元素是否以字符类开头"来区分。

## Transformer（L2.T5 - T7）

把解析树（规则名 + token）翻成语义 AST。本质是模式匹配 + 递归：

- `Number`/`String`/`TRUE`/`FALSE`/`NULL` → `ConstantExpression`
- `Identifier ('.' Identifier)?` → `ColumnRefExpression`
- 优先级链节点：两个孩子 → 比较；多于两个 → 按运算符折叠成左深树
- `count(*)` → `FunctionExpression{is_star=true}`

解析树有个坑：`Find(name)` 只查**直接**子节点——`TableRef` 在 `FromClause` 下面，要 `node.Find("FromClause")->Find("TableRef")`。

## Binder（L2.T8）

Binder 把"名字的世界"翻译成"下标的世界"：

1. **FROM**：`LogicalGet`（或 `LogicalJoin`）展开成 `BindScope`——每列记录 `{来源表, 列名, 类型}`；
2. **WHERE/SELECT/JOIN ON**：`BindScope::Resolve` 把 `ColumnRefExpression` 解析成 `BoundColumnRefExpression{column_index}`。限定名 `orders.o_custkey` 按表+列匹配；非限定名若匹配多列 → 报歧义错误；
3. **聚合重写**：`SELECT l_returnflag, count(*) FROM t GROUP BY l_returnflag` 拆成两层——`LogicalAggregate`（分组键 + 聚合）在下，`LogicalProjection` 在上。非分组键、非聚合的表达式在这里报错；
4. **类型推导**：比较 → BOOLEAN；算术取两操作数的较宽数值类型；`count → BIGINT`、`avg → DOUBLE`、`sum` 保持数值、`min/max` 保持输入类型；
5. **INSERT**：按目标列类型做强转校验（INT → DOUBLE 可以，VARCHAR → INT 报错）。

输出的逻辑计划形状（Lab 3 的输入）：

```
LogicalLimit? → LogicalOrder? → LogicalProjection → LogicalAggregate? → LogicalFilter? → LogicalGet | LogicalJoin
```

## 测试

```bash
./tdbtest   # Lab2ParserTest.*（12 个）+ Lab2BinderTest.*（11 个）
```

## 思考题

- 为什么 `SELECT` 列表中的表达式要在"聚合重写"这一步才区分聚合与非聚合，而不是在语法层？
- DuckDB 的 Binder 远比这里复杂（CTE、子查询、相关子查询解关联）。如果要支持 `WHERE a IN (SELECT ...)`，逻辑计划需要加什么节点？
