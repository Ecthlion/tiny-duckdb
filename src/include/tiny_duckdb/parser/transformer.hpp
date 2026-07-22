#pragma once

#include <memory>

#include "tiny_duckdb/parser/ast.hpp"
#include "tiny_duckdb/parser/peg.hpp"

namespace tiny_duckdb {

//! ============================================================================
//! LAB 2 (Tasks L2.T5-T7) - the Transformer turns a raw PEG parse tree into
//! typed SQL AST nodes (expressions + statements).
//!
//! Task L2.T5: TransformExpression / TransformLiteral
//! Task L2.T6: TransformSelect - select list (+ aliases), FROM (+ JOIN ON),
//!             WHERE, GROUP BY, ORDER BY, LIMIT.
//! Task L2.T7: TransformCreateTable / TransformInsert (short).
//! ============================================================================
class Transformer {
public:
	//! Entry point: transform the parse tree of the Statement rule
	std::unique_ptr<Statement> TransformStatement(const peg::Ast &statement);

private:
	std::unique_ptr<SelectStatement> TransformSelect(const peg::Ast &node);
	std::unique_ptr<CreateTableStatement> TransformCreateTable(const peg::Ast &node);
	std::unique_ptr<InsertStatement> TransformInsert(const peg::Ast &node);

	std::unique_ptr<Expression> TransformExpression(const peg::Ast &node);
	std::unique_ptr<Expression> TransformLiteral(const peg::Ast &node);
	ExpressionType TransformComparisonOp(const std::string &op);
	ExpressionType TransformArithmeticOp(const std::string &op);
};

} // namespace tiny_duckdb
