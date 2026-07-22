#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/exception.hpp"
#include "tiny_duckdb/common/types.hpp"

namespace tiny_duckdb {

class ExecutionContext;

//! ---------------------------------------------------------------------------
//! Operator states (subclassed by operators that need per-thread or shared
//! state). Mirrors DuckDB's source/sink state split.
//! ---------------------------------------------------------------------------
class OperatorState {
public:
	virtual ~OperatorState() = default;

	template <class T>
	T &Cast() {
		return static_cast<T &>(*this);
	}
	template <class T>
	const T &Cast() const {
		return static_cast<const T &>(*this);
	}
};

class GlobalSourceState {
public:
	virtual ~GlobalSourceState() = default;

	template <class T>
	T &Cast() {
		return static_cast<T &>(*this);
	}
	template <class T>
	const T &Cast() const {
		return static_cast<const T &>(*this);
	}
};

class GlobalSinkState {
public:
	virtual ~GlobalSinkState() = default;

	template <class T>
	T &Cast() {
		return static_cast<T &>(*this);
	}
	template <class T>
	const T &Cast() const {
		return static_cast<const T &>(*this);
	}
};

class LocalSinkState {
public:
	virtual ~LocalSinkState() = default;

	template <class T>
	T &Cast() {
		return static_cast<T &>(*this);
	}
	template <class T>
	const T &Cast() const {
		return static_cast<const T &>(*this);
	}
};

struct SourceInput {
	OperatorState &state;
	GlobalSourceState *global_state;
};

//! Result of an operator's Execute call (DuckDB-style):
//!   NEED_MORE_INPUT  the operator consumed the input; pull a fresh chunk
//!   HAVE_MORE_OUTPUT the operator has more output for the SAME logical input
//!                    (e.g. a join probe row with many matches); call it again
//!                    WITHOUT pulling a new source chunk
enum class OperatorResultType : uint8_t { NEED_MORE_INPUT = 0, HAVE_MORE_OUTPUT = 1 };

//! ============================================================================
//! LAB 3 - Push-based (morsel-driven) execution
//!
//! A physical operator can play up to three roles, exactly like in DuckDB:
//!
//!  * SOURCE: GetData() produces chunks (table scan; also aggregation/order/
//!    limit after their Finalize).
//!  * OPERATOR: Execute() transforms a chunk in the middle of a pipeline
//!    (filter, projection, join probe).
//!  * SINK: Sink()/Combine()/Finalize() consume chunks (aggregation, order,
//!    limit, join build side, result collector).
//!
//! A pipeline is: one source -> zero or more operators -> at most one sink.
//! ============================================================================
class PhysicalOperator {
public:
	PhysicalOperator(PhysicalOperatorType type, std::vector<LogicalType> types)
	    : type(type), types(std::move(types)) {
	}
	virtual ~PhysicalOperator() = default;

	PhysicalOperatorType type;
	//! Output schema
	std::vector<LogicalType> types;
	std::vector<std::string> names;
	std::vector<std::unique_ptr<PhysicalOperator>> children;

	template <class T>
	T &Cast() {
		return static_cast<T &>(*this);
	}
	template <class T>
	const T &Cast() const {
		return static_cast<const T &>(*this);
	}

	bool IsSink() const {
		return type == PhysicalOperatorType::HASH_GROUP_BY || type == PhysicalOperatorType::ORDER_BY ||
		       type == PhysicalOperatorType::LIMIT || type == PhysicalOperatorType::HASH_JOIN ||
		       type == PhysicalOperatorType::RESULT_COLLECTOR;
	}

	// --- source interface ---
	virtual std::unique_ptr<OperatorState> GetOperatorState(ExecutionContext &context);
	virtual std::unique_ptr<GlobalSourceState> GetGlobalSourceState(ExecutionContext &context);
	virtual void GetData(ExecutionContext &context, DataChunk &chunk, SourceInput &input);

	// --- operator interface ---
	virtual OperatorResultType Execute(ExecutionContext &context, DataChunk &chunk, OperatorState &state);

	// --- sink interface ---
	virtual std::unique_ptr<GlobalSinkState> GetGlobalSinkState(ExecutionContext &context);
	virtual std::unique_ptr<LocalSinkState> GetLocalSinkState(ExecutionContext &context, GlobalSinkState &gstate);
	virtual void Sink(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate,
	                  DataChunk &chunk);
	virtual void Combine(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate);
	virtual void Finalize(ExecutionContext &context, GlobalSinkState &gstate);
};

} // namespace tiny_duckdb
