/**
 * @file test-cord.c
 * @brief Your tests for the cord API.
 *
 * Write whatever tests you would like in this file.
 * main contains some examples to get you started.
 *
 * grade-cordlab will run this program, with no arguments, along with
 * its own tests.  Therefore, when run with no command-line arguments,
 * this program should run _all_ of your tests, and exit successfully
 * if they all succeeded, or unsuccessfully if at least one test
 * failed.  You can make it do something different when there are
 * arguments, if you want.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cord-interface.h"
#include "xalloc.h"

int main(int argc, char **argv) {
    const cord_t *c = cord_new("");
    assert(c == NULL);
    assert(cord_length(c) == 0);
    assert(cord_tostring(c) != NULL);

    return 0;
}
