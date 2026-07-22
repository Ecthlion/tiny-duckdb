#include "tiny_duckdb/parser/sql_grammar.hpp"

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

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
