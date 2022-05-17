/*
 * @file cord.c
 * @brief Implementation of cords library
 *
 * 15-513 Introduction to Computer Systems
 *
 * @author Jiyang Tang <jiyangta@andrew.cmu.edu>
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cord-interface.h"
#include "xalloc.h"

/***********************************/
/* Implementation (edit this part) */
/***********************************/

/**
 * @brief Checks if a cord is valid
 * @param[in] R
 * @return
 */
bool is_cord(const cord_t *R) {
    // printf("is_cord\n");
    // 1. NULL
    if (!R)
        return true;

    // 2. leaf
    if (!R->left && !R->right && R->data && R->len > 0 &&
        strlen(R->data) == R->len)
        return true;

    // 3. concat node
    if (R->left && R->right && // and!
        R->len > 0 && R->len == R->left->len + R->right->len) {
        return is_cord(R->left) && is_cord(R->right);
    }

    // no need to check for circles since the length check will fail

    return false;
}

/**
 * @brief Returns the length of a cord
 * @param[in] R
 * @return
 */
size_t cord_length(const cord_t *R) {
    // printf("cord_length\n");
    if (!R)
        return 0;
    return R->len;
}

/**
 * @brief Allocates a new cord from a string
 * @param[in] s
 * @return
 */
const cord_t *cord_new(const char *s) {
    // printf("cord_new: %s\n", s);
    size_t len = 0;
    if (!s || !(len = strlen(s)))
        return NULL;

    cord_t *ret = xmalloc(sizeof(cord_t));
    ret->data = s;
    ret->len = len;
    ret->left = NULL;
    ret->right = NULL;
    return ret;
}

/**
 * @brief Concatenates two cords into a new cord
 * @param[in] R
 * @param[in] S
 * @return
 */
const cord_t *cord_join(const cord_t *R, const cord_t *S) {
    // printf("cord_join\n");
    if (!R && !S) {
        return NULL;
    }

    if (!R)
        return S;
    if (!S)
        return R;

    size_t len = R->len + S->len;
    if (len < R->len || len < S->len) { // overflow
        return NULL;
    }

    cord_t *ret = xmalloc(sizeof(cord_t));
    ret->data = NULL;
    ret->len = len;
    ret->left = R;
    ret->right = S;
    return ret;
}

/**
 * @brief Converts a cord to a string
 * @param[in] R
 * @return
 */
char *cord_tostring(const cord_t *R) {
    if (!R) {
        char *result = xmalloc(1);
        result[1] = '\0';
        return result;
    }

    if (!R->left && !R->right) {
        return (char *)R->data;
    }

    char *result = xmalloc(cord_length(R) + 1);
    result[0] = '\0';
    strcat(result, cord_tostring(R->left));
    strcat(result, cord_tostring(R->right));
    return result;
}

/**
 * @brief Returns the character at a position in a cord
 *
 * @param[in] R  A cord
 * @param[in] i  A position in the cord
 * @return The character at position i
 *
 * @requires i is a valid position in the cord R
 */
char cord_charat(const cord_t *R, size_t i) {
    cord_t *p = (cord_t *)R;
    while (true) {
        assert(i <= p->len);

        if (i == R->len)
            return '\0';

        cord_t *left = (cord_t *)p->left;
        cord_t *right = (cord_t *)p->right;

        // 1. p is a leaf
        if (!left && !right)
            return p->data[i];

        // 2. p is a non-leaf
        if (left) {
            if (i < left->len) {
                p = (cord_t *)p->left;
                continue;
            } else
                i -= left->len;
        }
        if (right)
            p = (cord_t *)p->right;
    }
    return '\0';
}

static char *string_sub(const char *s, size_t lo, size_t hi) {
    size_t n = strlen(s);
    assert(lo <= hi && hi <= n);

    size_t len = hi - lo + 1;
    char *ret = xmalloc(len);
    ret[len - 1] = '\0';

    for (size_t i = lo, j = 0; i < hi; ++i, ++j)
        ret[j] = s[i];

    return ret;
}

/**
 * @brief Gets a substring of an existing cord
 *
 * @param[in] R   A cord
 * @param[in] lo  The low index of the substring, inclusive
 * @param[in] hi  The high index of the substring, exclusive
 * @return A cord representing the substring R[lo..hi-1]
 *
 * @requires lo and hi are valid indexes in R, with lo <= hi
 */
const cord_t *cord_sub(const cord_t *R, size_t lo, size_t hi) {
    assert(lo <= hi && hi <= cord_length(R));
    if (lo == hi)
        return NULL;

    if (lo == 0 && hi == R->len)
        return R;

    cord_t *p = (cord_t *)R;
    if (lo == hi)
        return cord_new("");

    cord_t *left = (cord_t *)p->left;
    cord_t *right = (cord_t *)p->right;

    // 1. p is a leaf
    if (!left && !right)
        return cord_new(string_sub(p->data, lo, hi));

    // 2. p is a non-leaf
    cord_t *left_ret = NULL;
    cord_t *right_ret = NULL;
    if (left) {
        if (lo < left->len) {
            left_ret =
                (cord_t *)cord_sub(left, lo, hi <= left->len ? hi : left->len);
        }

        // indices for the right child
        lo = lo > left->len ? lo - left->len : 0;
        hi = hi > left->len ? hi - left->len : 0;
    }
    if (right)
        right_ret = (cord_t *)cord_sub(right, lo, hi); // NULL if [0,0)

    return cord_join(left_ret, right_ret);
}
