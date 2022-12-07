cmake_minimum_required(VERSION 3.22)
project(cachelab C)

set(CMAKE_C_STANDARD 99)

include_directories(.)

add_executable(test-csim
        cachelab.c
        test-csim.c)
target_compile_options(test-csim PUBLIC
        -Wall -Wextra -Wpedantic -Wconversion -Wstrict-prototypes -Wwrite-strings -Wno-unused-parameter -Werror)

add_executable(csim cachelab.c csim.c)
target_compile_options(csim PUBLIC
        -Wall -Wextra -Wpedantic -Wconversion -Wstrict-prototypes -Wwrite-strings -Wno-unused-parameter -Werror)

add_executable(test-trans-simple trans.c test-trans-simple.c cachelab.c)
target_compile_options(test-trans-simple PUBLIC
        -Wall -Wextra -Wpedantic -Wconversion -Wstrict-prototypes -Wwrite-strings -Wno-unused-parameter -Werror)
