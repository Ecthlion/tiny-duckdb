# tiny-duckdb Makefile (primary build; CMakeLists.txt is provided as well)
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -pthread
INCLUDES := -Isrc/include -Ithird_party/tdbtest

SRC := $(shell find src -name '*.cpp')
OBJ := $(patsubst %.cpp,build/%.o,$(SRC))

TEST_SRC := $(wildcard test/*.cpp)
TEST_OBJ := $(patsubst %.cpp,build/%.o,$(TEST_SRC))

.PHONY: all test shell clean

all: tdbtest tiny_duckdb_shell

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

libtiny_duckdb.a: $(OBJ)
	ar rcs $@ $(OBJ)

tdbtest: $(TEST_OBJ) libtiny_duckdb.a
	$(CXX) $(CXXFLAGS) $(TEST_OBJ) -L. -ltiny_duckdb -o $@ -pthread

tiny_duckdb_shell: app/shell.cpp libtiny_duckdb.a
	$(CXX) $(CXXFLAGS) $(INCLUDES) app/shell.cpp -L. -ltiny_duckdb -o $@ -pthread

test: tdbtest
	./tdbtest

clean:
	rm -rf build libtiny_duckdb.a tdbtest tiny_duckdb_shell
