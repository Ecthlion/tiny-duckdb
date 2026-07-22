#include "tiny_duckdb/parser/parser.hpp"

#include "tiny_duckdb/parser/sql_grammar.hpp"
#include "tiny_duckdb/parser/transformer.hpp"

namespace tiny_duckdb {

SqlParser::SqlParser() : parser_(TinyDuckDBSqlGrammar()) {
}

std::unique_ptr<Statement> SqlParser::Parse(const std::string &sql) const {
	auto tree = parser_.Parse(sql);
	Transformer transformer;
	return transformer.TransformStatement(*tree);
}

std::unique_ptr<peg::Ast> SqlParser::ParseTree(const std::string &sql) const {
	return parser_.Parse(sql);
}

} // namespace tiny_duckdb
