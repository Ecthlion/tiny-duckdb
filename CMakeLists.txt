cmake_minimum_required(VERSION 3.16)
project(tiny_duckdb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to Debug")
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(TINY_DUCKDB_SANITIZER "address" CACHE STRING "Sanitizer used by Debug builds; empty disables sanitizers")

add_compile_options(-Wall -Wextra -Wno-unused-parameter -pthread)
add_link_options(-pthread)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND TINY_DUCKDB_SANITIZER
   AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	add_compile_options(-fsanitize=${TINY_DUCKDB_SANITIZER} -fno-omit-frame-pointer)
	add_link_options(-fsanitize=${TINY_DUCKDB_SANITIZER})
	message(STATUS "Debug sanitizer: ${TINY_DUCKDB_SANITIZER}")
endif()

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
foreach(LAB_NUMBER 0 1 2 3 5)
	add_test(NAME lab${LAB_NUMBER} COMMAND tdbtest Lab${LAB_NUMBER})
	set_tests_properties(lab${LAB_NUMBER} PROPERTIES LABELS "lab${LAB_NUMBER}")
endforeach()
