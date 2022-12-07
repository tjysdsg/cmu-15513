/**
 * @file An thread-safe LRU Cache, with string as keys, pointers as values
 */

#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Max cache and object sizes in bytes
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/**
 * Public interface for accessing key-value pair in the map
 *
 * Hides some members
 */
typedef struct cache_entry {
    const char *key;
    void *val;
    size_t size;
} cache_entry_t;

/**
 * Public interface for a string->pointer map
 *
 * Hides some members
 */
typedef struct {
    /// size in bytes of values stored in the map
    size_t size;
} cache_t;

/**
 * Create a cache
 */
cache_t *cache_create();

/**
 * Insert an item into the cache
 * @param cache Cache returned by cache_create
 * @param key String key
 * @param val Pointer value
 * @param size Size of the data
 * @return Created entry if success, otherwise NULL
 */
cache_entry_t *cache_insert(cache_t *cache, const char *key, void *val,
                            size_t size);

/**
 * Search key a key in the cache
 *
 * Increase reference count of the entry (if found)
 * Use cache_entry_release to decrement the refcount
 *
 * @param cache Cache returned by cache_create
 * @param key String key
 * @return Key-value pair if success, otherwise NULL
 */
cache_entry_t *cache_get(cache_t *cache, const char *key);

/**
 *
 * Release the reference to a cache entry
 * @param cache Cache returned by cache_create
 * @param e Cache entry
 */
void cache_entry_release(cache_t *cache, cache_entry_t *e);

#endif // CACHE_H
