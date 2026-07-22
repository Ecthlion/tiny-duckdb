#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tiny_duckdb/common/data_chunk.hpp"
#include "tiny_duckdb/common/enums.hpp"
#include "tiny_duckdb/common/types.hpp"

namespace tiny_duckdb {

class ExecutionContext;

//! ============================================================================
//! LAB 3 - push-based execution: the physical operator interface
//!
//! Every physical operator can play up to three roles (several at once):
//!
//!  * SOURCE   - produces chunks: GetData()
//!  * OPERATOR - transforms a chunk in place: Execute()
//!  * SINK     - consumes chunks: Sink() + Combine() + Finalize()
//!
//! Per-thread state is created through the Get*State methods; all shared
//! state lives in the GLOBAL state objects, thread-private state in the
//! operator/local state objects. This is how DuckDB keeps operators parallel.
//! ============================================================================
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

//! Input bundle for GetData(): the per-thread operator state + the shared global source state
struct SourceInput {
	OperatorState &state;
	GlobalSourceState *global_state;
};

class PhysicalOperator {
public:
	PhysicalOperator(PhysicalOperatorType type, std::vector<LogicalType> types)
	    : type(type), types(std::move(types)) {
	}
	virtual ~PhysicalOperator() = default;

	//! Operators that can be pipeline sinks
	bool IsSink() const {
		return type == PhysicalOperatorType::HASH_GROUP_BY || type == PhysicalOperatorType::ORDER_BY ||
		       type == PhysicalOperatorType::LIMIT || type == PhysicalOperatorType::HASH_JOIN ||
		       type == PhysicalOperatorType::RESULT_COLLECTOR;
	}

	// --- source interface ----------------------------------------------------
	virtual std::unique_ptr<OperatorState> GetOperatorState(ExecutionContext &context);
	virtual std::unique_ptr<GlobalSourceState> GetGlobalSourceState(ExecutionContext &context);
	virtual void GetData(ExecutionContext &context, DataChunk &chunk, SourceInput &input);

	// --- operator interface --------------------------------------------------
	virtual void Execute(ExecutionContext &context, DataChunk &chunk, OperatorState &state);

	// --- sink interface ------------------------------------------------------
	virtual std::unique_ptr<GlobalSinkState> GetGlobalSinkState(ExecutionContext &context);
	virtual std::unique_ptr<LocalSinkState> GetLocalSinkState(ExecutionContext &context, GlobalSinkState &gstate);
	virtual void Sink(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate, DataChunk &chunk);
	virtual void Combine(ExecutionContext &context, GlobalSinkState &gstate, LocalSinkState &lstate);
	virtual void Finalize(ExecutionContext &context, GlobalSinkState &gstate);

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
};

} // namespace tiny_duckdb
