# -*- Makefile -*- for cordlab (handout edition)

# Path to LLVM when running on autograder and shark cluster
ifneq (,$(wildcard /usr/lib/llvm-7/bin/))
  LLVM_PATH = /usr/lib/llvm-7/bin/
  LLVM_INCLUDE = /usr/lib/llvm-7/include
else
  ifneq (,$(wildcard /usr/local/depot/llvm-7.0/bin/))
    LLVM_PATH = /usr/local/depot/llvm-7.0/bin/
    LLVM_INCLUDE = /usr/local/depot/llvm-7.0/include
  endif
endif

CC = $(LLVM_PATH)clang
CFLAGS = -std=c11 -g -Og -Werror -Wall -Wextra -Wpedantic -Wconversion
CFLAGS += -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings
CFLAGS += -Wno-unused-parameter

PROGRAMS = test-cord grade-iscord grade-basic grade-rec grade-sub
all: $(PROGRAMS)

# Linking rules
test-cord: cord.o test-cord.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
grade-iscord: grade-iscord.o cord.o makesomecords.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
grade-basic: grade-basic.o cord.o makesomecords.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
grade-rec: grade-rec.o cord.o makesomecords.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
grade-sub: grade-sub.o cord.o makesomecords.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Header dependencies
cord.o: cord.c cord-interface.h xalloc.h
test-cord.o: test-cord.c cord-interface.h xalloc.h

# Tests
check: all
	./grade-cordlab

# Delete only cord.o and test-cord.o; the source code for the
# other .o files is not included in the handout.
clean:
	rm -f cord.o test-cord.o $(PROGRAMS)

.PHONY: all check clean

# Code formatting and submission is handled by helper.mk.
HANDOUT_SCRIPTS = grade-cordlab
HANDIN_TAR = cordlab-handin.tar
HANDIN_FILES = cord.c test-cord.c .clang-format .format-checked
FORMAT_FILES = cord.c test-cord.c
include helper.mk
