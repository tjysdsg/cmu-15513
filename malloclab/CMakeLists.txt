cmake_minimum_required(VERSION 3.20)
project(malloclab C)

set(CMAKE_C_STANDARD 11)

include_directories(.)
add_executable(mdriver
        mdriver.c
        memlib.c
        fcyc.c
        clock.c
        stree.c
        mm.c)
target_compile_options(mdriver PUBLIC
        -g -Werror -Wall -Wextra -Wpedantic -Wconversion
        -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings
        -Wno-unused-function -Wno-unused-parameter -Wno-zero-length-array
        -Wno-pointer-arith
        )
target_compile_definitions(mdriver PUBLIC DEBUG DRIVER)
