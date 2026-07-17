# Define project, Minimum CMake version, and enforce C99 standard
cmake_minimum_required(VERSION 3.15)
project(ina228_driver C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler-agnostic warning flags
if(MSVC)
    set(WARNING_FLAGS /W4 /WX)
else()
    set(WARNING_FLAGS -Wall -Wextra -Werror)
endif()

# Driver library
add_library(ina228 STATIC src/ina228.c)
target_include_directories(ina228 PUBLIC inc)
target_compile_options(ina228 PRIVATE ${WARNING_FLAGS})

# Host-side unit tests
enable_testing()

add_executable(test_ina228 tests/test_ina228.c)
target_link_libraries(test_ina228 PRIVATE ina228)
target_compile_options(test_ina228 PRIVATE ${WARNING_FLAGS})

# Link math library (libm) on non-MSVC platforms
if(NOT MSVC)
    target_link_libraries(test_ina228 PRIVATE m)
endif()

# Register the test executable with CTest
add_test(NAME test_ina228 COMMAND test_ina228)