#pragma once

#include <memory>
#include <string>

#include "tiny_duckdb/parser/ast.hpp"
#include "tiny_duckdb/parser/peg.hpp"

namespace tiny_duckdb {

//! The SQL parser: PEG grammar -> parse tree -> statement AST
class SqlParser {
public:
	SqlParser();

	//! Parse one SQL statement; throws ParserException on syntax errors
	std::unique_ptr<Statement> Parse(const std::string &sql) const;

	//! Parse and return the raw PEG tree (for debugging / lab tests)
	std::unique_ptr<peg::Ast> ParseTree(const std::string &sql) const;

private:
	peg::Parser parser_;
};

} // namespace tiny_duckdb
