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
    // printf("cord_tostring\n");
    char *result = malloc(cord_length(R) + 1);
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
    assert(i <= cord_length(R));

    char ret = '\0';
    // while (R->left || R->right) {
    // }
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
    return NULL;
}
