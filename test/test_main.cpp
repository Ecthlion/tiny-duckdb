#include "tdbtest.h"

int main(int argc, char **argv) {
	// optional substring filter: ./tdbtest Lab3ExecutionTest.Join
	return RUN_ALL_TESTS(argc > 1 ? argv[1] : "");
}
