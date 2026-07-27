# tiny-duckdb Makefile (primary build; CMakeLists.txt is provided as well)
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wno-unused-parameter -O2 -pthread
INCLUDES := -Isrc/include -Ithird_party/tdbtest

SRC := $(shell find src -name '*.cpp')
OBJ := $(patsubst %.cpp,build/%.o,$(SRC))

TEST_SRC := $(wildcard test/*.cpp)
TEST_OBJ := $(patsubst %.cpp,build/%.o,$(TEST_SRC))
DEP := $(OBJ:.o=.d) $(TEST_OBJ:.o=.d)

.PHONY: all test shell clean
.PHONY: format format-check install-hooks

all: tdbtest tiny_duckdb_shell

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP $(INCLUDES) -c $< -o $@

libtiny_duckdb.a: $(OBJ)
	ar rcs $@ $(OBJ)

tdbtest: $(TEST_OBJ) libtiny_duckdb.a
	$(CXX) $(CXXFLAGS) $(TEST_OBJ) -L. -ltiny_duckdb -o $@ -pthread

tiny_duckdb_shell: app/shell.cpp libtiny_duckdb.a
	$(CXX) $(CXXFLAGS) $(INCLUDES) app/shell.cpp -L. -ltiny_duckdb -o $@ -pthread

test: tdbtest
	./tdbtest

format:
	bash tools/clang_format.sh

format-check:
	bash tools/clang_format_check.sh

install-hooks:
	bash tools/setup_git_hooks.sh

clean:
	rm -rf build libtiny_duckdb.a tdbtest tiny_duckdb_shell

-include $(DEP)
