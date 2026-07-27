#include "tdbtest.h"

int main(int argc, char** argv) {
	if (argc > 1 && std::string(argv[1]) == "--list") {
		tdbtest::ListTests();
		return 0;
	}
	// optional substring filter: ./tdbtest Lab3ExecutionTest.Join
	return RUN_ALL_TESTS(argc > 1 ? argv[1] : "");
}
