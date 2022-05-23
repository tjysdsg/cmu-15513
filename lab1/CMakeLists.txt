cmake_minimum_required(VERSION 3.22)
project(lab1 C)

set(CMAKE_C_STANDARD 11)

include_directories(.)

add_executable(btest
        bits.c
        btest.c
        decl.c
        tests.c
        )
target_compile_options(btest PUBLIC -std=gnu99 -m64 -O1 -fwrapv -g -Werror -Wall -Wextra
        -Wstrict-prototypes -Wwrite-strings
        -Wno-unused-parameter -Wno-cast-function-type -Wno-bool-operation)
target_link_libraries(btest PUBLIC m)

add_executable(fshow fshow.c)
target_compile_options(fshow PUBLIC -std=gnu99 -m64 -O1 -fwrapv -g -Werror -Wall -Wextra
        -Wstrict-prototypes -Wwrite-strings
        -Wno-unused-parameter -Wno-cast-function-type -Wno-bool-operation)

add_executable(ishow ishow.c)
target_compile_options(ishow PUBLIC -std=gnu99 -m64 -O1 -fwrapv -g -Werror -Wall -Wextra
        -Wstrict-prototypes -Wwrite-strings
        -Wno-unused-parameter -Wno-cast-function-type -Wno-bool-operation)
