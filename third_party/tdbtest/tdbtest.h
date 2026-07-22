// tdbtest.h - a tiny gtest-style test framework for tiny-duckdb.
//
// Supports: TEST(suite, name), EXPECT_EQ / EXPECT_NE / EXPECT_TRUE /
// EXPECT_FALSE / EXPECT_THROW / EXPECT_NEAR, ASSERT_EQ, RUN_ALL_TESTS().
// Non-streamable types (enums) print as <unprintable> instead of failing
// to compile.
#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace tdbtest {

struct TestCase {
	std::string suite;
	std::string name;
	std::function<void()> fn;
};

inline std::vector<TestCase> &Registry() {
	static std::vector<TestCase> tests;
	return tests;
}

inline int &FailureCount() {
	static int failures = 0;
	return failures;
}

struct Registrar {
	Registrar(const std::string &suite, const std::string &name, std::function<void()> fn) {
		Registry().push_back({suite, name, std::move(fn)});
	}
};

// --- printing helpers (SFINAE: streamable or <unprintable>) ---
template <class T, class = void>
struct IsStreamable : std::false_type {};

template <class T>
struct IsStreamable<T, std::void_t<decltype(std::declval<std::ostream &>() << std::declval<const T &>())>>
    : std::true_type {};

template <class T>
std::string ToDebugString(const T &value) {
	if constexpr (IsStreamable<T>::value) {
		std::ostringstream out;
		out << value;
		return out.str();
	} else {
		return "<unprintable>";
	}
}

inline void ReportFailure(const char *file, int line, const std::string &message) {
	FailureCount()++;
	std::cout << file << ":" << line << ": Failure\n  " << message << "\n";
}

template <class A, class B>
bool ValuesEqual(const A &a, const B &b) {
	if constexpr (std::is_integral_v<A> && std::is_integral_v<B> &&
	              std::is_signed_v<A> != std::is_signed_v<B>) {
		// mixed signedness: reject negatives before widening
		if constexpr (std::is_signed_v<A>) {
			if (a < 0) {
				return false;
			}
			return static_cast<std::make_unsigned_t<A>>(a) == b;
		} else {
			if (b < 0) {
				return false;
			}
			return a == static_cast<std::make_unsigned_t<B>>(b);
		}
	} else {
		return a == b;
	}
}

template <class A, class B>
void ExpectEq(const A &actual, const B &expected, const char *actual_str, const char *expected_str,
              const char *file, int line) {
	if (!ValuesEqual(actual, expected)) {
		ReportFailure(file, line, std::string("Expected equality of ") + actual_str + " and " + expected_str +
		                             "\n    Actual: " + ToDebugString(actual) + "\n  Expected: " +
		                             ToDebugString(expected));
		throw std::runtime_error("expect_eq failed");
	}
}

template <class A, class B>
void ExpectNe(const A &actual, const B &expected, const char *actual_str, const char *expected_str,
              const char *file, int line) {
	if (ValuesEqual(actual, expected)) {
		ReportFailure(file, line, std::string("Expected inequality of ") + actual_str + " and " + expected_str);
		throw std::runtime_error("expect_ne failed");
	}
}

inline void ExpectTrue(bool condition, const char *expr_str, const char *file, int line) {
	if (!condition) {
		ReportFailure(file, line, std::string("Expected true: ") + expr_str);
		throw std::runtime_error("expect_true failed");
	}
}

inline void ExpectFalse(bool condition, const char *expr_str, const char *file, int line) {
	if (condition) {
		ReportFailure(file, line, std::string("Expected false: ") + expr_str);
		throw std::runtime_error("expect_false failed");
	}
}

inline void ExpectNear(double actual, double expected, double abs_error, const char *actual_str,
                       const char *expected_str, const char *file, int line) {
	if (std::fabs(actual - expected) > abs_error) {
		ReportFailure(file, line, std::string("Expected near: ") + actual_str + " vs " + expected_str +
		                             "\n    Actual: " + ToDebugString(actual) + "\n  Expected: " +
		                             ToDebugString(expected));
		throw std::runtime_error("expect_near failed");
	}
}

inline int RunAllTests(const std::string &filter = "") {
	int passed = 0;
	int failed = 0;
	for (const auto &test : Registry()) {
		// optional substring filter: ./tdbtest Lab0 runs only Lab0* suites
		const std::string full_name = test.suite + "." + test.name;
		if (!filter.empty() && test.suite.find(filter) == std::string::npos &&
		    full_name.find(filter) == std::string::npos) {
			continue;
		}
		int failures_before = FailureCount();
		std::cout << "[ RUN  ] " << test.suite << "." << test.name << "\n";
		try {
			test.fn();
		} catch (const std::exception &ex) {
			ReportFailure(__FILE__, __LINE__, std::string("Unhandled exception: ") + ex.what());
		} catch (...) {
			ReportFailure(__FILE__, __LINE__, "Unhandled unknown exception");
		}
		if (FailureCount() == failures_before) {
			passed++;
			std::cout << "[  OK  ] " << test.suite << "." << test.name << "\n";
		} else {
			failed++;
			std::cout << "[ FAIL ] " << test.suite << "." << test.name << "\n";
		}
	}
	std::cout << "========================================\n";
	if (!filter.empty()) {
		std::cout << "(filter: " << filter << ")\n";
	}
	std::cout << passed << " passed, " << failed << " failed\n";
	return failed == 0 ? 0 : 1;
}

} // namespace tdbtest

#define TEST(suite, name)                                                                                     \
	static void suite##_##name##_body();                                                                       \
	static ::tdbtest::Registrar suite##_##name##_registrar(#suite, #name, suite##_##name##_body);              \
	static void suite##_##name##_body()

#define EXPECT_EQ(actual, expected)                                                                            \
	::tdbtest::ExpectEq((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define EXPECT_NE(actual, expected)                                                                            \
	::tdbtest::ExpectNe((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define EXPECT_TRUE(expr) ::tdbtest::ExpectTrue((expr), #expr, __FILE__, __LINE__)
#define EXPECT_FALSE(expr) ::tdbtest::ExpectFalse((expr), #expr, __FILE__, __LINE__)
#define ASSERT_EQ(actual, expected) EXPECT_EQ(actual, expected)
#define ASSERT_TRUE(expr) EXPECT_TRUE(expr)
#define ASSERT_FALSE(expr) EXPECT_FALSE(expr)

#define EXPECT_NEAR(actual, expected, abs_error)                                                               \
	::tdbtest::ExpectNear((actual), (expected), (abs_error), #actual, #expected, __FILE__, __LINE__)

#define EXPECT_THROW(statement, exception_type)                                                                \
	do {                                                                                                       \
		bool caught_ = false;                                                                                   \
		try {                                                                                                   \
			statement;                                                                                           \
		} catch (const exception_type &) {                                                                      \
			caught_ = true;                                                                                      \
		} catch (const std::exception &ex) {                                                                    \
			::tdbtest::ReportFailure(__FILE__, __LINE__,                                                         \
			                         std::string("Wrong exception type: ") + ex.what());                        \
		}                                                                                                       \
		if (!caught_) {                                                                                         \
			::tdbtest::ReportFailure(__FILE__, __LINE__, "Expected exception: " #exception_type);               \
		}                                                                                                       \
	} while (0)

#define RUN_ALL_TESTS(filter) ::tdbtest::RunAllTests(filter)
