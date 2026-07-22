#include "tiny_duckdb/parser/sql_grammar.hpp"

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 2 (Tasks L2.T1 - L2.T4) - the SQL grammar (PEG)
//!
//! The whole SQL surface of tiny-duckdb is ONE PEG grammar string, compiled
//! by our packrat engine (peg.hpp - read its rule syntax first: '<-' is a
//! definition, '/' is ORDERED choice, '?' is optional, '*' is zero-or-more,
//! '!' is negation, and single quotes match case-insensitively).
//!
//! The four sub-tasks (docs/lab2.md walks through them one by one):
//!   L2.T1  the SELECT skeleton: SelectStmt, SelectList, FromClause,
//!          WhereClause, GroupByClause, OrderByClause, LimitClause
//!   L2.T2  the expression precedence chain: OrExpr -> AndExpr ->
//!          ComparisonExpr -> AdditiveExpr -> MultExpr -> UnaryExpr ->
//!          PrimaryExpr. Getting the CHAINING right is what makes
//!          "1 + 2 * 3" parse as add(1, mul(2, 3)).
//!   L2.T3  the JOIN clause: TableRef with an optional JoinClause
//!   L2.T4  CREATE TABLE and INSERT (ColumnDef, TypeName, RowList)
//!
//! Hint: build and test the grammar INCREMENTALLY - start with
//!       "SELECT * FROM t", run Lab2ParserTest.SelectStar, then add clauses
//!       one at a time. A PEG choice is ordered: put longer/more specific
//!       alternatives first (see ComparisonOp).
//! Hint: Keyword must come LAST in Identifier (`!Keyword` guard) or every
//!       keyword would parse as an identifier.
//!
//! Tests: test/lab2_parser_test.cpp (all Lab2ParserTest.* tests)
//! ============================================================================
const char *TinyDuckDBSqlGrammar() {
	// [SOLUTION BEGIN L2.T1-T4]
	return R"GRAMMAR(
Statement       <- SelectStmt / CreateTableStmt / InsertStmt

# --- SELECT (L2.T1-T4) ---
SelectStmt      <- 'select' SelectList FromClause WhereClause? GroupByClause? OrderByClause? LimitClause?
SelectList      <- SelectItem (',' SelectItem)*
SelectItem      <- Expression Alias? / Star
Star            <- '*'
Alias           <- 'as'? Identifier
FromClause      <- 'from' TableRef
TableRef        <- Identifier JoinClause?
JoinClause      <- 'inner'? 'join' Identifier 'on' Expression
WhereClause     <- 'where' Expression
GroupByClause   <- 'group' 'by' Expression (',' Expression)*
OrderByClause   <- 'order' 'by' OrderItem (',' OrderItem)*
OrderItem       <- Expression SortOrder?
SortOrder       <- 'asc' / 'desc'
LimitClause     <- 'limit' Number

# --- CREATE TABLE / INSERT (provided) ---
CreateTableStmt <- 'create' 'table' Identifier '(' ColumnDef (',' ColumnDef)* ')'
ColumnDef       <- Identifier TypeName
TypeName        <- 'integer' / 'int' / 'bigint' / 'double' / 'real' / 'varchar' / 'text' / 'boolean' / 'bool'
InsertStmt      <- 'insert' 'into' Identifier 'values' RowList
RowList         <- Row (',' Row)*
Row             <- '(' Literal (',' Literal)* ')'

# --- expression precedence chain (L2.T2) ---
Expression      <- OrExpr
OrExpr          <- AndExpr ('or' AndExpr)*
AndExpr         <- ComparisonExpr ('and' ComparisonExpr)*
ComparisonExpr  <- AdditiveExpr (ComparisonOp AdditiveExpr)?
ComparisonOp    <- '!=' / '<>' / '<=' / '>=' / '=' / '<' / '>'
AdditiveExpr    <- MultExpr (AdditiveOp MultExpr)*
AdditiveOp      <- '+' / '-'
MultExpr        <- UnaryExpr (MultOp UnaryExpr)*
MultOp          <- '*' / '/'
UnaryExpr       <- '-' UnaryExpr / PrimaryExpr
PrimaryExpr     <- '(' Expression ')' / FuncCall / Literal / ColumnRef
FuncCall        <- Identifier '(' (Star / Expression (',' Expression)*)? ')'
Literal         <- Number / String / 'true' / 'false' / 'null'
ColumnRef       <- Identifier ('.' Identifier)?
Number          <- [0-9]+ ('.' [0-9]+)?
String          <- ['] (!['] .)* [']
Identifier      <- !Keyword [a-zA-Z_] [a-zA-Z0-9_]*
Keyword         <- 'select' / 'from' / 'where' / 'group' / 'by' / 'order' / 'asc' / 'desc' / 'limit' / 'inner' / 'join' / 'on' / 'as' / 'and' / 'or' / 'create' / 'table' / 'insert' / 'into' / 'values' / 'true' / 'false' / 'null'
)GRAMMAR";
	// [SOLUTION END]
}

} // namespace tiny_duckdb
