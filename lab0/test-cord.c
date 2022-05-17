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
    {
        const cord_t *c = cord_new("");
        assert(c == NULL);
        assert(cord_length(c) == 0);
        assert(cord_tostring(c) != NULL);
    }

    {
        const cord_t *R1 = cord_join(cord_new("t"), cord_new("otally"));
        const cord_t *R2 = cord_join(cord_new("e"), cord_new("fficient"));
        const cord_t *c =
            cord_join(cord_join(R1, R2), cord_join(cord_new(","), R1));

        char charat = cord_charat(c, 2);
        assert(charat == 't');

        charat = cord_charat(c, 0);
        assert(charat == 't');

        charat = cord_charat(c, 7);
        assert(charat == 'e');

        charat = cord_charat(c, 15);
        assert(charat == 't');

        charat = cord_charat(c, 16);
        assert(charat == ',');

        charat = cord_charat(c, 17);
        assert(charat == 't');

        charat = cord_charat(c, 17);
        assert(charat == 't');
    }

    {
        const cord_t *R1 = cord_join(cord_new("t"), cord_new("otally"));
        const cord_t *R2 = cord_join(cord_new("e"), cord_new("fficient"));
        const cord_t *c = cord_join(R1, R2);

        const cord_t *res = cord_sub(c, 1, 16);
        printf("%s\n", cord_tostring(res));
    }

    return 0;
}
