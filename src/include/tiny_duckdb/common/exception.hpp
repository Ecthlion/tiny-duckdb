#pragma once

#include <stdexcept>
#include <string>

namespace tiny_duckdb {

//! Base class for all tiny-duckdb exceptions
class Exception : public std::exception {
public:
	explicit Exception(std::string message) : message_(std::move(message)) {
	}

	const char *what() const noexcept override {
		return message_.c_str();
	}

private:
	std::string message_;
};

//! Thrown by lab stubs that students have not implemented yet
class NotImplementedException : public Exception {
public:
	explicit NotImplementedException(const std::string &what)
	    : Exception("Not implemented: " + what + ". Have you finished the corresponding lab task?") {
	}
};

class ParserException : public Exception {
public:
	explicit ParserException(const std::string &msg) : Exception("Parser error: " + msg) {
	}
};

class BinderException : public Exception {
public:
	explicit BinderException(const std::string &msg) : Exception("Binder error: " + msg) {
	}
};

class CatalogException : public Exception {
public:
	explicit CatalogException(const std::string &msg) : Exception("Catalog error: " + msg) {
	}
};

class ExecutorException : public Exception {
public:
	explicit ExecutorException(const std::string &msg) : Exception("Executor error: " + msg) {
	}
};

class StorageException : public Exception {
public:
	explicit StorageException(const std::string &msg) : Exception("Storage error: " + msg) {
	}
};

} // namespace tiny_duckdb
