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
const char* TinyDuckDBSqlGrammar() {
	// TODO(L2.T1-T4): implement this (see the corresponding docs/labN.md)
	throw NotImplementedException("task L2.T1-T4 not implemented yet");
}

} // namespace tiny_duckdb
