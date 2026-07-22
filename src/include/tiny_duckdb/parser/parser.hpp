#pragma once

#include <memory>
#include <string>

#include "tiny_duckdb/parser/ast.hpp"
#include "tiny_duckdb/parser/peg.hpp"

namespace tiny_duckdb {

//! The SQL parser: PEG grammar + transformer. Compiles the grammar once.
class SqlParser {
public:
	SqlParser();

	//! Parse one SQL statement. Throws ParserException on syntax errors.
	std::unique_ptr<Statement> Parse(const std::string &sql) const;

	//! Expose the raw PEG parse tree (used by the Lab 2 tests)
	std::unique_ptr<peg::Ast> ParseTree(const std::string &sql) const;

private:
	peg::Parser parser_;
};

} // namespace tiny_duckdb
