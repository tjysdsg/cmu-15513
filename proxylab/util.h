#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/**
 * Allocate memory with specified content
 * @param src Source memory to copy from
 * @param size Size of the memory
 * @return Initialized memory
 *
 * Never return NULL. Will print error using perror and exit program if failed
 */
void *malloc_with_data(void *src, size_t size);

#endif // UTIL_H
