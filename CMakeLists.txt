cmake_minimum_required(VERSION 3.16)
project(tiny_duckdb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")

file(GLOB_RECURSE TINY_DUCKDB_SOURCES src/*.cpp)

add_library(tiny_duckdb STATIC ${TINY_DUCKDB_SOURCES})
target_include_directories(tiny_duckdb PUBLIC src/include)

add_executable(tiny_duckdb_shell app/shell.cpp)
target_link_libraries(tiny_duckdb_shell tiny_duckdb)

file(GLOB TEST_SOURCES test/*.cpp)
add_executable(tdbtest ${TEST_SOURCES})
target_include_directories(tdbtest PRIVATE third_party/tdbtest)
target_link_libraries(tdbtest tiny_duckdb)

enable_testing()
add_test(NAME tdbtest COMMAND tdbtest)
