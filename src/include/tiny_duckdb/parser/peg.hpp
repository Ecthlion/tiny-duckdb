#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tiny_duckdb/common/types.hpp"

namespace tiny_duckdb {
namespace peg {

//! ============================================================================
//! LAB 2 - PEG parsing (provided infrastructure)
//!
//! A small PEG (Parsing Expression Grammar) packrat parser, in the spirit of
//! cpp-peglib. A grammar is a set of rules:
//!
//!     Expr    <- Term (PLUS Term)*
//!     Term    <- [0-9]+
//!
//! Supported operators: sequence, ordered choice (/), and-predicate (&),
//! not-predicate (!), option (?), zero-or-more (*), one-or-more (+),
//! 'literal' (case-insensitive), "literal", [char-class], [^negated-class],
//! . (any char) and ( grouping ).
//!
//! Every named rule produces an Ast node holding the matched text (token)
//! and the nodes of the rules it referenced. Whitespace is skipped
//! automatically before sequence elements - except before raw char classes,
//! so lexeme rules like Identifier <- [a-zA-Z_] [a-zA-Z0-9_]* stay contiguous.
//! ============================================================================
struct Ast {
	std::string name;
	std::string token;
	std::vector<std::unique_ptr<Ast>> children;
	idx_t start = 0;
	idx_t end = 0;

	//! First direct child with the given rule name, or nullptr
	const Ast *Find(const std::string &child_name) const;
	//! All direct children with the given rule name
	std::vector<const Ast *> FindAll(const std::string &child_name) const;
	//! Deep copy
	std::unique_ptr<Ast> Clone() const;
	//! Debug tree dump
	std::string ToString(idx_t depth = 0) const;
};

class Parser {
public:
	//! Compile a grammar; the first rule is the start rule. Throws ParserException.
	explicit Parser(const std::string &grammar);

	//! Parse the whole input. Throws ParserException with line/column on failure.
	std::unique_ptr<Ast> Parse(const std::string &input) const;

private:
	struct Expression {
		enum class Kind : uint8_t {
			LITERAL,
			CHAR_CLASS,
			DOT,
			REFERENCE,
			SEQUENCE,
			CHOICE,
			AND_PREDICATE,
			NOT_PREDICATE,
			OPTION,
			STAR,
			PLUS
		};

		Kind kind;
		std::string text; // LITERAL text / CHAR_CLASS spec / REFERENCE name
		std::vector<std::unique_ptr<Expression>> children;
		std::unique_ptr<Expression> inner;
	};

	struct Rule {
		std::string name;
		std::unique_ptr<Expression> expression;
	};

	struct MemoEntry {
		bool success = false;
		idx_t end = 0;
		std::unique_ptr<Ast> node;
	};

	struct MatchContext {
		const std::string *input = nullptr;
		std::unordered_map<idx_t, MemoEntry> memo;
		idx_t farthest_failure = 0;
	};

	// grammar parsing
	std::vector<std::unique_ptr<Rule>> ParseGrammar(const std::string &grammar) const;
	std::unique_ptr<Expression> ParseChoice(const std::string &text, idx_t &pos) const;
	std::unique_ptr<Expression> ParseSequence(const std::string &text, idx_t &pos) const;
	std::unique_ptr<Expression> ParsePrefix(const std::string &text, idx_t &pos) const;
	std::unique_ptr<Expression> ParseSuffix(const std::string &text, idx_t &pos) const;
	std::unique_ptr<Expression> ParseTerm(const std::string &text, idx_t &pos) const;

	// matching
	bool Match(const Expression &expression, MatchContext &context, idx_t &pos,
	           std::vector<std::unique_ptr<Ast>> &nodes) const;
	bool MatchRule(idx_t rule_index, MatchContext &context, idx_t &pos,
	               std::vector<std::unique_ptr<Ast>> &nodes) const;
	bool MatchCharClass(const std::string &spec, char c) const;
	static bool StartsWithCharClass(const Expression &expression);

	static void SkipWhitespace(const std::string &input, idx_t &pos);
	static bool IsIdentifierChar(char c);

	std::vector<std::unique_ptr<Rule>> rules_;
};

} // namespace peg
} // namespace tiny_duckdb
