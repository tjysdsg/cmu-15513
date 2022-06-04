cmake_minimum_required(VERSION 3.22)
project(lab0 C)

set(CMAKE_C_STANDARD 11)

include_directories(.)

add_executable(test-cord
        cord-interface.h
        cord.c
        test-cord.c
        xalloc.h)
