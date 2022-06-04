/**
 * @file cord-interface.h
 * @brief The cords library interface
 */

#ifndef CORD_INTERFACE_H
#define CORD_INTERFACE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct cord_t cord_t;
struct cord_t {
    size_t len;
    const cord_t *left;
    const cord_t *right;
    const char *data;
};

/** @brief Checks if a cord is valid */
bool is_cord(const cord_t *R);

/** @brief Returns the length of a cord */
size_t cord_length(const cord_t *R);

/** @brief Allocates a new cord from a string */
const cord_t *cord_new(const char *s);

/** @brief Concatenates two cords into a new cord */
const cord_t *cord_join(const cord_t *R, const cord_t *S);

/** @brief Converts a cord to a string */
char *cord_tostring(const cord_t *R);

/** @brief Returns the character at a position in a cord */
char cord_charat(const cord_t *R, size_t i);

/** @brief Gets a substring of an existing cord */
const cord_t *cord_sub(const cord_t *R, size_t lo, size_t hi);

#endif /* CORD_INTERFACE_H */
