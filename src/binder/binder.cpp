#include "tiny_duckdb/binder/binder.hpp"

#include <set>
#include <utility>

#include "tiny_duckdb/common/exception.hpp"

namespace tiny_duckdb {

static std::string Normalize(const std::string &name) {
	return Catalog::NormalizeName(name);
}

idx_t BindScope::Resolve(const ColumnRefExpression &ref) const {
	const std::string column = Normalize(ref.column);
	if (ref.IsQualified()) {
		const std::string table = Normalize(ref.table);
		for (idx_t i = 0; i < names.size(); i++) {
			if (names[i] == column && tables[i] == table) {
				return i;
			}
		}
		throw BinderException("Column not found: " + ref.ToString());
	}
	idx_t found = static_cast<idx_t>(-1);
	for (idx_t i = 0; i < names.size(); i++) {
		if (names[i] == column) {
			if (found != static_cast<idx_t>(-1)) {
				throw BinderException("Ambiguous column reference: " + ref.column +
				                      " (qualify it with a table name)");
			}
			found = i;
		}
	}
	if (found == static_cast<idx_t>(-1)) {
		throw BinderException("Column not found: " + ref.column);
	}
	return found;
}

namespace {

bool IsAggregateName(const std::string &name) {
	const std::string lowered = Normalize(name);
	return lowered == "count" || lowered == "sum" || lowered == "avg" || lowered == "min" || lowered == "max";
}

bool IsVectorDistanceName(const std::string &name) {
	const std::string lowered = Normalize(name);
	return lowered == "l2_distance" || lowered == "array_distance" ||
	       lowered == "cosine_distance" || lowered == "array_cosine_distance" ||
	       lowered == "negative_inner_product" || lowered == "array_negative_inner_product";
}

bool ContainsAggregate(Expression &expression) {
	if (expression.type == ExpressionType::AGGREGATE_COUNT) {
		// FunctionExpression stores AGGREGATE_COUNT as a placeholder type
		auto &function = static_cast<FunctionExpression &>(expression);
		if (IsAggregateName(function.name)) {
			return true;
		}
		if (IsVectorDistanceName(function.name)) {
			return false;
		}
		throw BinderException("Unknown function: " + function.name);
	}
	if (expression.type >= ExpressionType::COMPARE_EQUAL &&
	    expression.type <= ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL) {
		auto &comparison = static_cast<ComparisonExpression &>(expression);
		return ContainsAggregate(*comparison.left) || ContainsAggregate(*comparison.right);
	}
	if (expression.type == ExpressionType::CONJUNCTION_AND || expression.type == ExpressionType::CONJUNCTION_OR) {
		auto &conjunction = static_cast<ConjunctionExpression &>(expression);
		return ContainsAggregate(*conjunction.left) || ContainsAggregate(*conjunction.right);
	}
	if (expression.type >= ExpressionType::OPERATOR_ADD && expression.type <= ExpressionType::OPERATOR_DIVIDE) {
		auto &op = static_cast<OperatorExpression &>(expression);
		return ContainsAggregate(*op.left) || ContainsAggregate(*op.right);
	}
	return false;
}

void SplitConjunction(Expression &expression, std::vector<Expression *> &conjuncts) {
	if (expression.type == ExpressionType::CONJUNCTION_AND) {
		auto &conjunction = static_cast<ConjunctionExpression &>(expression);
		SplitConjunction(*conjunction.left, conjuncts);
		SplitConjunction(*conjunction.right, conjuncts);
		return;
	}
	conjuncts.push_back(&expression);
}

} // namespace

//! LAB 5 - TASK #2: bind a scalar vector-distance function. This is where
//! arity, VECTOR argument types, and equal dimensions are checked before any
//! rows are scanned.
std::unique_ptr<BoundExpression> Binder::BindVectorDistance(FunctionExpression &function,
                                                            const BindScope &scope) {
	// [SOLUTION BEGIN L5.T2]
	const std::string name = Normalize(function.name);
	if (!IsVectorDistanceName(name)) {
		throw BinderException("Unknown vector function: " + function.name);
	}
	if (function.is_star || function.args.size() != 2) {
		throw BinderException(name + " takes exactly two VECTOR arguments");
	}
	auto left = BindExpression(*function.args[0], scope);
	auto right = BindExpression(*function.args[1], scope);
	if (left->return_type.Id() != LogicalTypeId::VECTOR || right->return_type.Id() != LogicalTypeId::VECTOR) {
		throw BinderException(name + " requires VECTOR arguments, got " + left->return_type.ToString() +
		                      " and " + right->return_type.ToString());
	}
	if (left->return_type != right->return_type) {
		throw BinderException(name + " requires equal dimensions, got " + left->return_type.ToString() +
		                      " and " + right->return_type.ToString());
	}
	VectorDistanceType distance_type = VectorDistanceType::L2;
	if (name == "cosine_distance" || name == "array_cosine_distance") {
		distance_type = VectorDistanceType::COSINE;
	} else if (name == "negative_inner_product" || name == "array_negative_inner_product") {
		distance_type = VectorDistanceType::NEGATIVE_INNER_PRODUCT;
	}
	return std::make_unique<BoundVectorDistanceExpression>(distance_type, std::move(left), std::move(right),
	                                                       name);
	// [SOLUTION END]
}

//! ----------------------------------------------------------------------------
//! Task L2.T8a - Binder::BindExpression
//!
//! Bind one AST expression against a BindScope (the columns produced by the
//! FROM clause), producing a BoundExpression with a resolved type:
//!   VALUE_CONSTANT      -> BoundConstantExpression
//!   COLUMN_REF          -> BoundColumnRefExpression via scope.Resolve (it
//!                          throws BinderException on unknown or AMBIGUOUS
//!                          names - do not catch it, that is the test)
//!   COMPARE_*           -> bind both sides; the result is Boolean
//!   CONJUNCTION_AND/OR  -> bind both sides; the result is Boolean
//!   OPERATOR_+-*/       -> both operands must be numeric
//!                          (BinderException otherwise); the result type is
//!                          Value::MaxNumericType(left, right), except
//!                          DIVIDE which is always Double
//!   aggregate functions -> they NEVER appear here (see BindSelect): reaching
//!                          one means it sits in an illegal position such as
//!                          a WHERE clause - throw BinderException
//!
//! Tests: Lab2BinderTest.BindUnknownColumnThrows / BindAmbiguousColumnThrows /
//!        BindQualifiedResolvesAmbiguity / BindArithmeticTypePromotion
//! ----------------------------------------------------------------------------
std::unique_ptr<BoundExpression> Binder::BindExpression(Expression &expression, const BindScope &scope) {
	// [SOLUTION BEGIN L2.T8]
	switch (expression.type) {
	case ExpressionType::VALUE_CONSTANT: {
		auto &constant = static_cast<ConstantExpression &>(expression);
		return std::make_unique<BoundConstantExpression>(constant.value);
	}
	case ExpressionType::COLUMN_REF: {
		if (dynamic_cast<StarExpression *>(&expression) != nullptr) {
			throw BinderException("* is only valid as a bare SELECT item");
		}
		auto &ref = static_cast<ColumnRefExpression &>(expression);
		const idx_t index = scope.Resolve(ref);
		return std::make_unique<BoundColumnRefExpression>(scope.names[index], index, scope.types[index]);
	}
	case ExpressionType::COMPARE_EQUAL:
	case ExpressionType::COMPARE_NOT_EQUAL:
	case ExpressionType::COMPARE_LESS_THAN:
	case ExpressionType::COMPARE_LESS_THAN_OR_EQUAL:
	case ExpressionType::COMPARE_GREATER_THAN:
	case ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL: {
		auto &comparison = static_cast<ComparisonExpression &>(expression);
		auto left = BindExpression(*comparison.left, scope);
		auto right = BindExpression(*comparison.right, scope);
		return std::make_unique<BoundComparisonExpression>(expression.type, std::move(left), std::move(right));
	}
	case ExpressionType::CONJUNCTION_AND:
	case ExpressionType::CONJUNCTION_OR: {
		auto &conjunction = static_cast<ConjunctionExpression &>(expression);
		auto left = BindExpression(*conjunction.left, scope);
		auto right = BindExpression(*conjunction.right, scope);
		return std::make_unique<BoundConjunctionExpression>(expression.type, std::move(left), std::move(right));
	}
	case ExpressionType::OPERATOR_ADD:
	case ExpressionType::OPERATOR_SUBTRACT:
	case ExpressionType::OPERATOR_MULTIPLY:
	case ExpressionType::OPERATOR_DIVIDE: {
		auto &op = static_cast<OperatorExpression &>(expression);
		auto left = BindExpression(*op.left, scope);
		auto right = BindExpression(*op.right, scope);
		if (!left->return_type.IsNumeric() || !right->return_type.IsNumeric()) {
			throw BinderException("Arithmetic requires numeric operands, got " + left->return_type.ToString() +
			                      " and " + right->return_type.ToString());
		}
		LogicalType result_type = Value::MaxNumericType(left->return_type, right->return_type);
		if (expression.type == ExpressionType::OPERATOR_DIVIDE) {
			result_type = LogicalType::Double();
		}
		return std::make_unique<BoundOperatorExpression>(expression.type, std::move(left), std::move(right),
		                                                 result_type);
	}
	case ExpressionType::AGGREGATE_COUNT: {
		// a FunctionExpression reached outside the aggregation rewrite
		auto &function = static_cast<FunctionExpression &>(expression);
		if (IsVectorDistanceName(function.name)) {
			return BindVectorDistance(function, scope);
		}
		if (IsAggregateName(function.name)) {
			throw BinderException("Aggregate " + function.name + " is not allowed in this clause");
		}
		throw BinderException("Unknown function: " + function.name);
	}
	default:
		throw BinderException("Cannot bind expression");
	}
	// [SOLUTION END]
}

//! ----------------------------------------------------------------------------
//! Task L2.T8b - Binder::BindAggregate
//!
//! Bind one aggregate FunctionExpression (count/sum/avg/min/max) into a
//! BoundAggregateExpression. Rules:
//!   count(*)        -> AGGREGATE_COUNT_STAR, no child, returns BigInt
//!   count(col)      -> AGGREGATE_COUNT, returns BigInt
//!   sum / avg       -> numeric child only (BinderException otherwise),
//!                      return Double
//!   min / max       -> any type, returns the CHILD's type
//!   unknown name    -> BinderException
//!
//! Tests: Lab2BinderTest.BindAggregateTypes / BindAggregateRewrite
//! ----------------------------------------------------------------------------
std::unique_ptr<BoundAggregateExpression> Binder::BindAggregate(FunctionExpression &function,
                                                                const BindScope &scope) {
	// [SOLUTION BEGIN L2.T8]
	const std::string name = Normalize(function.name);
	if (!IsAggregateName(name)) {
		throw BinderException("Unknown function: " + function.name);
	}
	if (name == "count" && function.is_star) {
		if (!function.args.empty()) {
			throw BinderException("count(*) takes no arguments");
		}
		return std::make_unique<BoundAggregateExpression>(ExpressionType::AGGREGATE_COUNT_STAR, nullptr,
		                                                  LogicalType::BigInt());
	}
	if (function.args.size() != 1) {
		throw BinderException("Aggregate " + name + " takes exactly one argument");
	}
	auto child = BindExpression(*function.args[0], scope);
	if (name == "count") {
		return std::make_unique<BoundAggregateExpression>(ExpressionType::AGGREGATE_COUNT, std::move(child),
		                                                  LogicalType::BigInt());
	}
	if (name == "sum" || name == "avg") {
		if (!child->return_type.IsNumeric()) {
			throw BinderException(name + " requires a numeric argument");
		}
		return std::make_unique<BoundAggregateExpression>(
		    name == "sum" ? ExpressionType::AGGREGATE_SUM : ExpressionType::AGGREGATE_AVG, std::move(child),
		    LogicalType::Double());
	}
	// min / max keep the input type
	return std::make_unique<BoundAggregateExpression>(
	    name == "min" ? ExpressionType::AGGREGATE_MIN : ExpressionType::AGGREGATE_MAX, std::move(child),
	    child->return_type);
	// [SOLUTION END]
}

//! ----------------------------------------------------------------------------
//! Task L2.T8c - Binder::RewriteAfterAggregate
//!
//! THE key transformation of the binder. A select list like
//!   SELECT l_returnflag, sum(l_quantity) + 1 ... GROUP BY l_returnflag
//! mixes three kinds of sub-expressions; the LogicalAggregate can only
//! produce [group keys..., aggregate results...], so this function rewrites
//! each select-list expression to run ABOVE the aggregation:
//!   * a sub-expression equal to a GROUP BY key  -> BoundColumnRefExpression
//!     pointing at that group's output slot
//!   * an aggregate function                     -> collected into
//!     `aggregates`, replaced by a BoundColumnRefExpression pointing at its
//!     output slot (group_count + aggregate_index)
//!   * anything else (constants, arithmetic over the two above) -> rebuilt
//!     recursively with the same operator and rewritten children
//!
//! Hint: compare a column ref with a GROUP BY key by their NORMALIZED table
//!       and column names (the provided Normalize() helper); a ref that
//!       matches no group key is a BinderException ("must appear in the
//!       GROUP BY clause or inside an aggregate").
//!
//! Tests: Lab2BinderTest.BindAggregateRewrite / BindAggregateArithmeticRewrite
//! ----------------------------------------------------------------------------
std::unique_ptr<BoundExpression> Binder::RewriteAfterAggregate(
    Expression &expression, const std::vector<Expression *> &group_asts,
    const std::vector<LogicalType> &group_types,
    std::vector<std::unique_ptr<BoundAggregateExpression>> &aggregates, const BindScope &scope) {
	// [SOLUTION BEGIN L2.T8]
	if (expression.type == ExpressionType::AGGREGATE_COUNT) {
		auto &function = static_cast<FunctionExpression &>(expression);
		auto bound = BindAggregate(function, scope);
		const LogicalType return_type = bound->return_type;
		const std::string name = function.ToString();
		aggregates.push_back(std::move(bound));
		return std::make_unique<BoundColumnRefExpression>(name, group_types.size() + aggregates.size() - 1,
		                                                  return_type);
	}
	if (expression.type == ExpressionType::COLUMN_REF) {
		auto &ref = static_cast<ColumnRefExpression &>(expression);
		for (idx_t g = 0; g < group_asts.size(); g++) {
			if (group_asts[g]->type != ExpressionType::COLUMN_REF) {
				continue;
			}
			auto &group_ref = static_cast<ColumnRefExpression &>(*group_asts[g]);
			if (Normalize(group_ref.column) == Normalize(ref.column) &&
			    Normalize(group_ref.table) == Normalize(ref.table)) {
				return std::make_unique<BoundColumnRefExpression>(group_ref.column, g, group_types[g]);
			}
		}
		throw BinderException("Column " + ref.ToString() +
		                      " must appear in the GROUP BY clause or inside an aggregate");
	}
	if (expression.type == ExpressionType::VALUE_CONSTANT) {
		auto &constant = static_cast<ConstantExpression &>(expression);
		return std::make_unique<BoundConstantExpression>(constant.value);
	}
	if (expression.type >= ExpressionType::COMPARE_EQUAL &&
	    expression.type <= ExpressionType::COMPARE_GREATER_THAN_OR_EQUAL) {
		auto &comparison = static_cast<ComparisonExpression &>(expression);
		auto left = RewriteAfterAggregate(*comparison.left, group_asts, group_types, aggregates, scope);
		auto right = RewriteAfterAggregate(*comparison.right, group_asts, group_types, aggregates, scope);
		return std::make_unique<BoundComparisonExpression>(expression.type, std::move(left), std::move(right));
	}
	if (expression.type == ExpressionType::CONJUNCTION_AND || expression.type == ExpressionType::CONJUNCTION_OR) {
		auto &conjunction = static_cast<ConjunctionExpression &>(expression);
		auto left = RewriteAfterAggregate(*conjunction.left, group_asts, group_types, aggregates, scope);
		auto right = RewriteAfterAggregate(*conjunction.right, group_asts, group_types, aggregates, scope);
		return std::make_unique<BoundConjunctionExpression>(expression.type, std::move(left), std::move(right));
	}
	if (expression.type >= ExpressionType::OPERATOR_ADD && expression.type <= ExpressionType::OPERATOR_DIVIDE) {
		auto &op = static_cast<OperatorExpression &>(expression);
		auto left = RewriteAfterAggregate(*op.left, group_asts, group_types, aggregates, scope);
		auto right = RewriteAfterAggregate(*op.right, group_asts, group_types, aggregates, scope);
		LogicalType result_type = Value::MaxNumericType(left->return_type, right->return_type);
		if (expression.type == ExpressionType::OPERATOR_DIVIDE) {
			result_type = LogicalType::Double();
		}
		return std::make_unique<BoundOperatorExpression>(expression.type, std::move(left), std::move(right),
		                                                 result_type);
	}
	throw BinderException("Unsupported expression in aggregate query");
	// [SOLUTION END]
}

//! ----------------------------------------------------------------------------
//! Task L2.T8d - Binder::BindSelect
//!
//! Assemble the logical plan for a SELECT, bottom-up:
//!   LogicalLimit?            (only with LIMIT)
//!    └ LogicalOrder?         (only with ORDER BY; keys are (output column
//!    └─index, ascending) pairs resolved against the SELECT LIST, not the
//!      base table)
//!     └ LogicalProjection    (the select list)
//!      └ LogicalAggregate?   (only with GROUP BY or any aggregate function)
//!       └ LogicalFilter?     (only with WHERE)
//!        └ LogicalGet | LogicalJoin
//!
//! Steps: bind FROM (LogicalGet over the catalog table, or LogicalJoin with
//! the equi-condition split by SplitConjunction into left/right key pairs -
//! reject non-equi or same-side conditions with BinderException) -> build the
//! BindScope -> WHERE -> detect aggregates (ContainsAggregate) and either
//! bind the select list directly or through RewriteAfterAggregate -> ORDER
//! BY -> LIMIT. Fill the BoundStatement's output names (alias if present,
//! else "table.column" for column refs) and types along the way.
//!
//! Tests: Lab2BinderTest.BindSimpleSelect / BindStarExpands /
//!        BindWhereProducesFilter / BindOrderAndLimit / BindMultipleGroupKeys
//! ----------------------------------------------------------------------------
std::unique_ptr<BoundStatement> Binder::BindSelect(SelectStatement &statement) {
	// [SOLUTION BEGIN L2.T8]
	auto result = std::make_unique<BoundStatement>(StatementType::SELECT_STATEMENT);

	// ---- 1. FROM (+ JOIN) ---------------------------------------------------
	TableData &left_table = catalog_.GetTable(statement.table);
	std::vector<idx_t> left_ids;
	for (idx_t i = 0; i < left_table.ColumnCount(); i++) {
		left_ids.push_back(i);
	}
	auto left_get = std::make_unique<LogicalGet>(left_table, left_ids);

	BindScope scope;
	for (idx_t i = 0; i < left_table.ColumnCount(); i++) {
		scope.tables.push_back(Normalize(statement.table));
		scope.names.push_back(left_table.GetColumnNames()[i]);
		scope.types.push_back(left_table.GetColumnTypes()[i]);
	}

	std::unique_ptr<LogicalOperator> input;
	if (statement.has_join) {
		TableData &right_table = catalog_.GetTable(statement.join_table);
		const idx_t left_width = scope.ColumnCount();
		std::vector<idx_t> right_ids;
		for (idx_t i = 0; i < right_table.ColumnCount(); i++) {
			right_ids.push_back(i);
			scope.tables.push_back(Normalize(statement.join_table));
			scope.names.push_back(right_table.GetColumnNames()[i]);
			scope.types.push_back(right_table.GetColumnTypes()[i]);
		}
		auto right_get = std::make_unique<LogicalGet>(right_table, right_ids);

		auto join = std::make_unique<LogicalJoin>();
		std::vector<Expression *> conjuncts;
		SplitConjunction(*statement.join_condition, conjuncts);
		for (Expression *conjunct : conjuncts) {
			if (conjunct->type != ExpressionType::COMPARE_EQUAL) {
				throw BinderException("Only equi-join conditions (a = b) are supported");
			}
			auto &comparison = static_cast<ComparisonExpression &>(*conjunct);
			if (comparison.left->type != ExpressionType::COLUMN_REF ||
			    comparison.right->type != ExpressionType::COLUMN_REF) {
				throw BinderException("Join conditions must be column = column");
			}
			auto &left_ref = static_cast<ColumnRefExpression &>(*comparison.left);
			auto &right_ref = static_cast<ColumnRefExpression &>(*comparison.right);
			idx_t left_index = scope.Resolve(left_ref);
			idx_t right_index = scope.Resolve(right_ref);
			// normalize: key on the left child first, key on the right child second
			if (left_index >= left_width) {
				std::swap(left_index, right_index);
			}
			if (right_index < left_width || left_index >= left_width) {
				throw BinderException("Join keys must reference both sides of the join");
			}
			auto left_key = std::make_unique<BoundColumnRefExpression>(scope.names[left_index], left_index,
			                                                           scope.types[left_index]);
			auto right_key = std::make_unique<BoundColumnRefExpression>(
			    scope.names[right_index], right_index - left_width, scope.types[right_index]);
			join->conditions.emplace_back(std::move(left_key), std::move(right_key));
		}
		join->names = scope.names;
		join->types = scope.types;
		join->children.push_back(std::move(left_get));
		join->children.push_back(std::move(right_get));
		input = std::move(join);
	} else {
		input = std::move(left_get);
	}

	// ---- 2. WHERE -----------------------------------------------------------
	if (statement.where) {
		auto predicate = BindExpression(*statement.where, scope);
		auto filter = std::make_unique<LogicalFilter>(std::move(predicate));
		filter->names = scope.names;
		filter->types = scope.types;
		filter->children.push_back(std::move(input));
		input = std::move(filter);
	}

	// ---- 3. SELECT list -----------------------------------------------------
	// expand `*`
	std::vector<std::unique_ptr<Expression>> select_list;
	std::vector<std::string> select_aliases;
	for (idx_t i = 0; i < statement.select_list.size(); i++) {
		if (dynamic_cast<StarExpression *>(statement.select_list[i].get()) != nullptr) {
			for (idx_t c = 0; c < scope.ColumnCount(); c++) {
				select_list.push_back(std::make_unique<ColumnRefExpression>(scope.tables[c], scope.names[c]));
				select_aliases.push_back("");
			}
			continue;
		}
		select_list.push_back(std::move(statement.select_list[i]));
		select_aliases.push_back(statement.select_aliases[i]);
	}

	bool aggregate_mode = !statement.group_by.empty();
	for (const auto &expression : select_list) {
		if (ContainsAggregate(*expression)) {
			aggregate_mode = true;
		}
	}

	std::vector<std::unique_ptr<BoundExpression>> bound_select;
	std::vector<std::string> output_names;
	std::vector<LogicalType> output_types;

	if (aggregate_mode) {
		std::vector<std::unique_ptr<BoundExpression>> groups;
		std::vector<Expression *> group_asts;
		std::vector<LogicalType> group_types;
		for (auto &group : statement.group_by) {
			group_asts.push_back(group.get());
			auto bound_group = BindExpression(*group, scope);
			group_types.push_back(bound_group->return_type);
			groups.push_back(std::move(bound_group));
		}

		std::vector<std::unique_ptr<BoundAggregateExpression>> aggregates;
		for (idx_t i = 0; i < select_list.size(); i++) {
			auto rewritten = RewriteAfterAggregate(*select_list[i], group_asts, group_types, aggregates, scope);
			output_types.push_back(rewritten->return_type);
			output_names.push_back(select_aliases[i].empty() ? select_list[i]->ToString() : select_aliases[i]);
			bound_select.push_back(std::move(rewritten));
		}

		auto aggregate = std::make_unique<LogicalAggregate>(std::move(groups), std::move(aggregates));
		for (idx_t g = 0; g < statement.group_by.size(); g++) {
			aggregate->names.push_back(statement.group_by[g]->ToString());
			aggregate->types.push_back(aggregate->groups[g]->return_type);
		}
		for (const auto &agg : aggregate->aggregates) {
			aggregate->names.push_back(agg->ToString());
			aggregate->types.push_back(agg->return_type);
		}
		aggregate->children.push_back(std::move(input));
		input = std::move(aggregate);
	} else {
		for (idx_t i = 0; i < select_list.size(); i++) {
			auto bound = BindExpression(*select_list[i], scope);
			output_types.push_back(bound->return_type);
			output_names.push_back(select_aliases[i].empty() ? select_list[i]->ToString() : select_aliases[i]);
			bound_select.push_back(std::move(bound));
		}
	}

	auto projection = std::make_unique<LogicalProjection>(std::move(bound_select));
	projection->names = output_names;
	projection->types = output_types;
	projection->children.push_back(std::move(input));
	input = std::move(projection);

	// ---- 4. ORDER BY --------------------------------------------------------
	if (!statement.order_by.empty()) {
		auto order = std::make_unique<LogicalOrder>();
		for (const auto &item : statement.order_by) {
			if (item.expression->type != ExpressionType::COLUMN_REF) {
				throw BinderException("ORDER BY only supports output column names");
			}
			auto &ref = static_cast<ColumnRefExpression &>(*item.expression);
			const std::string target = Normalize(ref.column);
			idx_t found = static_cast<idx_t>(-1);
			for (idx_t i = 0; i < output_names.size(); i++) {
				if (Normalize(output_names[i]) == target) {
					found = i;
				}
			}
			if (found == static_cast<idx_t>(-1)) {
				throw BinderException("ORDER BY column not found in SELECT list: " + ref.column);
			}
			order->keys.emplace_back(found, item.ascending);
		}
		order->names = output_names;
		order->types = output_types;
		order->children.push_back(std::move(input));
		input = std::move(order);
	}

	// ---- 5. LIMIT -----------------------------------------------------------
	if (statement.has_limit) {
		auto limit = std::make_unique<LogicalLimit>(statement.limit);
		limit->names = output_names;
		limit->types = output_types;
		limit->children.push_back(std::move(input));
		input = std::move(limit);
	}

	result->plan = std::move(input);
	result->names = output_names;
	result->types = output_types;
	return result;
	// [SOLUTION END]
}

std::unique_ptr<BoundStatement> Binder::BindCreateTable(CreateTableStatement &statement) {
	auto result = std::make_unique<BoundStatement>(StatementType::CREATE_TABLE_STATEMENT);
	if (statement.columns.empty()) {
		throw BinderException("CREATE TABLE requires at least one column");
	}
	std::set<std::string> seen;
	for (const auto &column : statement.columns) {
		if (!seen.insert(Normalize(column.name)).second) {
			throw BinderException("Duplicate column name: " + column.name);
		}
	}
	result->table_name = statement.table;
	result->columns = statement.columns;
	return result;
}

//! ----------------------------------------------------------------------------
//! Task L2.T8e - Binder::BindInsert
//!
//! Bind an INSERT: look the table up in the catalog, check the row width
//! against the column count, and coerce every literal Value to the target
//! column type:
//!   * numeric -> numeric: widen via GetNumeric (an INTEGER literal into a
//!     DOUBLE column is fine, and vice versa);
//!   * VARCHAR -> VARCHAR passes through;
//!   * 'null' becomes a typed NULL for the column;
//!   * everything else (e.g. a string into an INTEGER column) is a
//!     BinderException.
//!
//! Tests: Lab2BinderTest.BindInsertCoercesTypes
//! ----------------------------------------------------------------------------
std::unique_ptr<BoundStatement> Binder::BindInsert(InsertStatement &statement) {
	auto result = std::make_unique<BoundStatement>(StatementType::INSERT_STATEMENT);
	TableData &table = catalog_.GetTable(statement.table);
	for (const auto &row : statement.rows) {
		if (row.size() != table.ColumnCount()) {
			throw BinderException("INSERT row has " + std::to_string(row.size()) + " values but table " +
			                      statement.table + " has " + std::to_string(table.ColumnCount()) + " columns");
		}
		std::vector<Value> values;
		for (idx_t col = 0; col < row.size(); col++) {
			if (row[col]->type != ExpressionType::VALUE_CONSTANT) {
				throw BinderException("INSERT only supports literal values");
			}
			const Value &value = static_cast<ConstantExpression &>(*row[col]).value;
			const LogicalType &target = table.GetColumnTypes()[col];
			if (value.IsNull()) {
				values.push_back(Value::Null(target));
				continue;
			}
			switch (target.Id()) {
			case LogicalTypeId::INTEGER:
				if (value.GetType().Id() != LogicalTypeId::INTEGER) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into INTEGER column");
				}
				values.push_back(value);
				break;
			case LogicalTypeId::BIGINT:
				if (!value.GetType().IsIntegral()) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into BIGINT column");
				}
				values.push_back(Value::BigInt(value.GetIntegral()));
				break;
			case LogicalTypeId::DOUBLE:
				if (!value.GetType().IsNumeric()) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into DOUBLE column");
				}
				values.push_back(Value::Double(value.GetNumeric()));
				break;
			case LogicalTypeId::VARCHAR:
				if (value.GetType().Id() != LogicalTypeId::VARCHAR) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into VARCHAR column");
				}
				values.push_back(value);
				break;
			case LogicalTypeId::BOOLEAN:
				if (value.GetType().Id() != LogicalTypeId::BOOLEAN) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into BOOLEAN column");
				}
				values.push_back(value);
				break;
			case LogicalTypeId::VECTOR:
				if (value.GetType() != target) {
					throw BinderException("Cannot insert " + value.GetType().ToString() + " into " +
					                      target.ToString() + " column");
				}
				values.push_back(value);
				break;
			}
		}
		result->rows.push_back(std::move(values));
	}
	result->insert_table = &table;
	return result;
}

std::unique_ptr<BoundStatement> Binder::Bind(Statement &statement) {
	switch (statement.type) {
	case StatementType::SELECT_STATEMENT:
		return BindSelect(static_cast<SelectStatement &>(statement));
	case StatementType::CREATE_TABLE_STATEMENT:
		return BindCreateTable(static_cast<CreateTableStatement &>(statement));
	case StatementType::INSERT_STATEMENT:
		return BindInsert(static_cast<InsertStatement &>(statement));
	}
	throw BinderException("Unknown statement type");
}

} // namespace tiny_duckdb
