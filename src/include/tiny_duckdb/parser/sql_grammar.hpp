#pragma once

#include <string>

namespace tiny_duckdb {

//! ============================================================================
//! LAB 2 (Tasks L2.T1-T4) - the tiny-duckdb SQL grammar, written in PEG.
//!
//! Rule syntax: sequence, / (ordered choice), ? * +, & ! (predicates),
//! 'literal' (case-insensitive), [char-class], . (any char), ( grouping ).
//! Whitespace is skipped automatically before sequence elements (except
//! before raw char classes).
//!
//! Task L2.T1: SELECT ... FROM ... with literal/column expressions
//! Task L2.T2: the full expression precedence chain (OR/AND/comparison/
//!             additive/multiplicative/unary) + WHERE
//! Task L2.T3: GROUP BY / aggregate function calls
//! Task L2.T4: ORDER BY / LIMIT
//! (CREATE TABLE / INSERT are provided so the database is usable end-to-end.)
//! ============================================================================
const char *TinyDuckDBSqlGrammar();

} // namespace tiny_duckdb
