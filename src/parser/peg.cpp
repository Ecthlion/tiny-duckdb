#include "tiny_duckdb/parser/peg.hpp"

#include <cctype>
#include <sstream>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {
namespace peg {

namespace {
//! Whitespace inside the GRAMMAR text itself: spaces/tabs only. Newlines
//! terminate a rule's expression, so grammar parsing must not skip them.
void SkipInlineWhitespace(const std::string &text, idx_t &pos) {
	while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r')) {
		pos++;
	}
}
} // namespace

const Ast *Ast::Find(const std::string &child_name) const {
	for (const auto &child : children) {
		if (child->name == child_name) {
			return child.get();
		}
	}
	return nullptr;
}

std::vector<const Ast *> Ast::FindAll(const std::string &child_name) const {
	std::vector<const Ast *> result;
	for (const auto &child : children) {
		if (child->name == child_name) {
			result.push_back(child.get());
		}
	}
	return result;
}

std::unique_ptr<Ast> Ast::Clone() const {
	auto copy = std::make_unique<Ast>();
	copy->name = name;
	copy->token = token;
	copy->start = start;
	copy->end = end;
	for (const auto &child : children) {
		copy->children.push_back(child->Clone());
	}
	return copy;
}

std::string Ast::ToString(idx_t depth) const {
	std::string result(depth * 2, ' ');
	result += name;
	result += " '" + token + "'";
	result += "\n";
	for (const auto &child : children) {
		result += child->ToString(depth + 1);
	}
	return result;
}

void Parser::SkipWhitespace(const std::string &input, idx_t &pos) {
	while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos]))) {
		pos++;
	}
}

bool Parser::IsIdentifierChar(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

Parser::Parser(const std::string &grammar) {
	rules_ = ParseGrammar(grammar);
	if (rules_.empty()) {
		throw ParserException("Empty PEG grammar");
	}
}

std::unique_ptr<Ast> Parser::Parse(const std::string &input) const {
	MatchContext context;
	context.input = &input;
	idx_t pos = 0;
	std::vector<std::unique_ptr<Ast>> nodes;
	SkipWhitespace(input, pos);
	if (!MatchRule(0, context, pos, nodes)) {
		const std::string &text = input;
		idx_t line = 1;
		idx_t column = 1;
		for (idx_t i = 0; i < context.farthest_failure && i < text.size(); i++) {
			if (text[i] == '\n') {
				line++;
				column = 1;
			} else {
				column++;
			}
		}
		std::ostringstream message;
		message << "syntax error at line " << line << ", column " << column;
		throw ParserException(message.str());
	}
	SkipWhitespace(input, pos);
	if (pos != input.size()) {
		throw ParserException("unexpected trailing input at offset " + std::to_string(pos));
	}
	if (nodes.empty()) {
		throw ParserException("internal error: parse produced no AST");
	}
	return std::move(nodes.back());
}

// ---------------------------------------------------------------------------
// Grammar parsing
// ---------------------------------------------------------------------------

std::vector<std::unique_ptr<Parser::Rule>> Parser::ParseGrammar(const std::string &grammar) const {
	std::vector<std::unique_ptr<Rule>> rules;
	idx_t pos = 0;
	while (pos < grammar.size()) {
		SkipWhitespace(grammar, pos); // full skip: newlines separate rules
		if (pos >= grammar.size()) {
			break;
		}
		if (grammar[pos] == '#') { // comment to end of line
			while (pos < grammar.size() && grammar[pos] != '\n') {
				pos++;
			}
			continue;
		}
		if (!std::isalpha(static_cast<unsigned char>(grammar[pos])) && grammar[pos] != '_') {
			throw ParserException("PEG grammar: expected rule name at offset " + std::to_string(pos));
		}
		auto rule = std::make_unique<Rule>();
		idx_t start = pos;
		while (pos < grammar.size() && IsIdentifierChar(grammar[pos])) {
			pos++;
		}
		rule->name = grammar.substr(start, pos - start);
		SkipInlineWhitespace(grammar, pos);
		if (pos + 1 >= grammar.size() || grammar[pos] != '<' || grammar[pos + 1] != '-') {
			throw ParserException("PEG grammar: expected <- after rule " + rule->name);
		}
		pos += 2;
		rule->expression = ParseChoice(grammar, pos);
		rules.push_back(std::move(rule));
	}
	return rules;
}

std::unique_ptr<Parser::Expression> Parser::ParseChoice(const std::string &text, idx_t &pos) const {
	std::vector<std::unique_ptr<Expression>> alternatives;
	alternatives.push_back(ParseSequence(text, pos));
	while (true) {
		SkipInlineWhitespace(text, pos);
		if (pos < text.size() && text[pos] == '/') {
			pos++;
			alternatives.push_back(ParseSequence(text, pos));
		} else {
			break;
		}
	}
	if (alternatives.size() == 1) {
		return std::move(alternatives[0]);
	}
	auto choice = std::make_unique<Expression>();
	choice->kind = Expression::Kind::CHOICE;
	choice->children = std::move(alternatives);
	return choice;
}

std::unique_ptr<Parser::Expression> Parser::ParseSequence(const std::string &text, idx_t &pos) const {
	std::vector<std::unique_ptr<Expression>> elements;
	while (true) {
		SkipInlineWhitespace(text, pos);
		if (pos >= text.size()) {
			break;
		}
		const char c = text[pos];
		if (c == '\n' || c == '/' || c == ')' || c == '#') {
			break;
		}
		elements.push_back(ParsePrefix(text, pos));
	}
	if (elements.empty()) {
		throw ParserException("PEG grammar: empty sequence at offset " + std::to_string(pos));
	}
	if (elements.size() == 1) {
		return std::move(elements[0]);
	}
	auto sequence = std::make_unique<Expression>();
	sequence->kind = Expression::Kind::SEQUENCE;
	sequence->children = std::move(elements);
	return sequence;
}

std::unique_ptr<Parser::Expression> Parser::ParsePrefix(const std::string &text, idx_t &pos) const {
	if (text[pos] == '&' || text[pos] == '!') {
		const char op = text[pos];
		pos++;
		auto expression = std::make_unique<Expression>();
		expression->kind = op == '&' ? Expression::Kind::AND_PREDICATE : Expression::Kind::NOT_PREDICATE;
		expression->inner = ParsePrefix(text, pos);
		return expression;
	}
	return ParseSuffix(text, pos);
}

std::unique_ptr<Parser::Expression> Parser::ParseSuffix(const std::string &text, idx_t &pos) const {
	auto term = ParseTerm(text, pos);
	if (pos < text.size() && (text[pos] == '?' || text[pos] == '*' || text[pos] == '+')) {
		const char op = text[pos];
		pos++;
		auto expression = std::make_unique<Expression>();
		if (op == '?') {
			expression->kind = Expression::Kind::OPTION;
		} else if (op == '*') {
			expression->kind = Expression::Kind::STAR;
		} else {
			expression->kind = Expression::Kind::PLUS;
		}
		expression->inner = std::move(term);
		return expression;
	}
	return term;
}

std::unique_ptr<Parser::Expression> Parser::ParseTerm(const std::string &text, idx_t &pos) const {
	const char c = text[pos];
	if (c == '(') {
		pos++;
		auto inner = ParseChoice(text, pos);
		SkipInlineWhitespace(text, pos);
		if (pos >= text.size() || text[pos] != ')') {
			throw ParserException("PEG grammar: expected ) at offset " + std::to_string(pos));
		}
		pos++;
		return inner;
	}
	if (c == '\'' || c == '"') {
		const char quote = c;
		pos++;
		idx_t start = pos;
		while (pos < text.size() && text[pos] != quote) {
			pos++;
		}
		if (pos >= text.size()) {
			throw ParserException("PEG grammar: unterminated literal");
		}
		auto expression = std::make_unique<Expression>();
		expression->kind = Expression::Kind::LITERAL;
		expression->text = text.substr(start, pos - start);
		pos++;
		return expression;
	}
	if (c == '[') {
		pos++;
		idx_t start = pos;
		while (pos < text.size() && text[pos] != ']') {
			pos++;
		}
		if (pos >= text.size()) {
			throw ParserException("PEG grammar: unterminated character class");
		}
		auto expression = std::make_unique<Expression>();
		expression->kind = Expression::Kind::CHAR_CLASS;
		expression->text = text.substr(start, pos - start);
		pos++;
		return expression;
	}
	if (c == '.') {
		pos++;
		auto expression = std::make_unique<Expression>();
		expression->kind = Expression::Kind::DOT;
		return expression;
	}
	if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
		idx_t start = pos;
		while (pos < text.size() && IsIdentifierChar(text[pos])) {
			pos++;
		}
		auto expression = std::make_unique<Expression>();
		expression->kind = Expression::Kind::REFERENCE;
		expression->text = text.substr(start, pos - start);
		return expression;
	}
	throw ParserException(std::string("PEG grammar: unexpected character '") + c + "' at offset " +
	                      std::to_string(pos));
}

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

//! Does this expression start with a raw character class (or '.')? Whitespace
//! is skipped automatically BEFORE sequence elements - except before raw char
//! classes, so that lexeme rules like Identifier <- [a-zA-Z_] [a-zA-Z0-9_]*
//! stay contiguous.
bool Parser::StartsWithCharClass(const Expression &expression) {
	using Kind = Expression::Kind;
	switch (expression.kind) {
	case Kind::CHAR_CLASS:
	case Kind::DOT:
		return true;
	case Kind::AND_PREDICATE:
	case Kind::NOT_PREDICATE:
	case Kind::OPTION:
	case Kind::STAR:
	case Kind::PLUS:
		return StartsWithCharClass(*expression.inner);
	case Kind::SEQUENCE:
		return StartsWithCharClass(*expression.children[0]);
	case Kind::CHOICE:
		for (const auto &alternative : expression.children) {
			if (StartsWithCharClass(*alternative)) {
				return true;
			}
		}
		return false;
	default:
		return false;
	}
}

bool Parser::MatchCharClass(const std::string &spec, char c) const {
	bool negate = false;
	idx_t pos = 0;
	if (pos < spec.size() && spec[pos] == '^') {
		negate = true;
		pos++;
	}
	bool matched = false;
	while (pos < spec.size()) {
		if (pos + 2 < spec.size() && spec[pos + 1] == '-') {
			if (c >= spec[pos] && c <= spec[pos + 2]) {
				matched = true;
			}
			pos += 3;
		} else {
			if (c == spec[pos]) {
				matched = true;
			}
			pos++;
		}
	}
	return negate ? !matched : matched;
}

bool Parser::MatchRule(idx_t rule_index, MatchContext &context, idx_t &pos,
                       std::vector<std::unique_ptr<Ast>> &nodes) const {
	const std::string &input = *context.input;
	const idx_t key = rule_index * (input.size() + 1) + pos;
	const auto memo = context.memo.find(key);
	if (memo != context.memo.end()) {
		if (!memo->second.success) {
			return false;
		}
		pos = memo->second.end;
		nodes.push_back(memo->second.node->Clone());
		return true;
	}

	const Rule &rule = *rules_[rule_index];
	const idx_t start = pos;
	std::vector<std::unique_ptr<Ast>> children;
	MemoEntry entry;
	if (Match(*rule.expression, context, pos, children)) {
		auto node = std::make_unique<Ast>();
		node->name = rule.name;
		node->token = input.substr(start, pos - start);
		node->start = start;
		node->end = pos;
		node->children = std::move(children);
		entry.success = true;
		entry.end = pos;
		entry.node = std::move(node);
		nodes.push_back(entry.node->Clone());
		context.memo.emplace(key, std::move(entry));
		return true;
	}
	pos = start;
	entry.success = false;
	context.memo.emplace(key, std::move(entry));
	return false;
}

bool Parser::Match(const Expression &expression, MatchContext &context, idx_t &pos,
                   std::vector<std::unique_ptr<Ast>> &nodes) const {
	const std::string &input = *context.input;
	switch (expression.kind) {
	case Expression::Kind::LITERAL: {
		const std::string &literal = expression.text;
		bool matches = pos + literal.size() <= input.size();
		if (matches) {
			for (idx_t i = 0; i < literal.size(); i++) {
				if (std::tolower(static_cast<unsigned char>(input[pos + i])) !=
				    std::tolower(static_cast<unsigned char>(literal[i]))) {
					matches = false;
					break;
				}
			}
		}
		// keyword boundary: a literal ending in a word character must not be
		// immediately followed by another word character
		if (matches && IsIdentifierChar(literal.back()) && pos + literal.size() < input.size() &&
		    IsIdentifierChar(input[pos + literal.size()])) {
			matches = false;
		}
		if (!matches) {
			if (pos > context.farthest_failure) {
				context.farthest_failure = pos;
			}
			return false;
		}
		pos += literal.size();
		return true;
	}
	case Expression::Kind::CHAR_CLASS: {
		if (pos >= input.size() || !MatchCharClass(expression.text, input[pos])) {
			if (pos > context.farthest_failure) {
				context.farthest_failure = pos;
			}
			return false;
		}
		pos++;
		return true;
	}
	case Expression::Kind::DOT: {
		if (pos >= input.size()) {
			return false;
		}
		pos++;
		return true;
	}
	case Expression::Kind::REFERENCE: {
		for (idx_t i = 0; i < rules_.size(); i++) {
			if (rules_[i]->name == expression.text) {
				return MatchRule(i, context, pos, nodes);
			}
		}
		throw ParserException("PEG grammar: unknown rule referenced: " + expression.text);
	}
	case Expression::Kind::SEQUENCE: {
		const idx_t start = pos;
		const idx_t node_start = nodes.size();
		for (const auto &element : expression.children) {
			if (!StartsWithCharClass(*element)) {
				SkipWhitespace(input, pos);
			}
			if (!Match(*element, context, pos, nodes)) {
				pos = start;
				nodes.resize(node_start);
				return false;
			}
		}
		return true;
	}
	case Expression::Kind::CHOICE: {
		for (const auto &alternative : expression.children) {
			const idx_t start = pos;
			const idx_t node_start = nodes.size();
			if (Match(*alternative, context, pos, nodes)) {
				return true;
			}
			pos = start;
			nodes.resize(node_start);
		}
		return false;
	}
	case Expression::Kind::AND_PREDICATE: {
		const idx_t start = pos;
		const idx_t node_start = nodes.size();
		const bool result = Match(*expression.inner, context, pos, nodes);
		pos = start;
		nodes.resize(node_start);
		return result;
	}
	case Expression::Kind::NOT_PREDICATE: {
		const idx_t start = pos;
		const idx_t node_start = nodes.size();
		const bool result = Match(*expression.inner, context, pos, nodes);
		pos = start;
		nodes.resize(node_start);
		return !result;
	}
	case Expression::Kind::OPTION: {
		const idx_t start = pos;
		const idx_t node_start = nodes.size();
		if (!Match(*expression.inner, context, pos, nodes)) {
			pos = start;
			nodes.resize(node_start);
		}
		return true;
	}
	case Expression::Kind::STAR: {
		while (true) {
			const idx_t start = pos;
			const idx_t node_start = nodes.size();
			if (!Match(*expression.inner, context, pos, nodes) || pos == start) {
				pos = start;
				nodes.resize(node_start);
				return true;
			}
		}
	}
	case Expression::Kind::PLUS: {
		idx_t matched = 0;
		while (true) {
			const idx_t start = pos;
			const idx_t node_start = nodes.size();
			if (!Match(*expression.inner, context, pos, nodes) || pos == start) {
				pos = start;
				nodes.resize(node_start);
				return matched > 0;
			}
			matched++;
		}
	}
	}
	throw ParserException("internal error: unknown PEG expression kind");
}

} // namespace peg
} // namespace tiny_duckdb
