/**
 * @file An thread-safe LRU Cache, with string as keys, pointers as values
 *
 * TODO: free removed entries
 */

#include "cache.h"
#include "csapp.h"
#include "debug.h"
#include "util.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/**
 * Internal representation of map key-value pair as circular linked list
 *
 * The public interface should not contain the next pointer
 *
 * The linked list is ordered by access time (most recent at front)
 *
 * @see cache_entry
 */
typedef struct entry {
    const char *key;
    void *val;
    size_t size;
    struct entry *next;
    struct entry *prev;
    int ref;
} entry_t;

/**
 * Internal representation of map
 *
 * @see cache_t
 */
typedef struct {
    size_t size;
    entry_t *head;
    pthread_mutex_t cache_mutex;
} _cache_t;

// prototypes

static entry_t *find(_cache_t *cache, const char *key);
static void remove_entry(_cache_t *cache, entry_t *e);
static void evict_entry(_cache_t *cache, entry_t *e);
static void insert_front(_cache_t *cache, entry_t *e);
static void cache_mutex_lock(_cache_t *cache);
static void cache_mutex_unlock(_cache_t *cache);
static void free_entry(entry_t *e);

cache_t *cache_create() {
    _cache_t *cache = Calloc(1, sizeof(_cache_t));
    if (pthread_mutex_init(&cache->cache_mutex, NULL)) {
        sio_eprintf("Failed to init cache mutex\n");
        return NULL;
    }
    return (cache_t *)cache;
}

cache_entry_t *cache_insert(cache_t *_cache, const char *key, void *val,
                            size_t size) {
    if (size > MAX_OBJECT_SIZE) {
        return NULL;
    }
    _cache_t *cache = (_cache_t *)_cache;

    cache_mutex_lock(cache);
    entry_t *e = find(cache, key);

    if (!e) {
        e = Calloc(1, sizeof(entry_t));
        e->key = strdup(key);
        e->val = malloc_with_data(val, size);
        e->size = size;
        e->ref = 1;

        insert_front(cache, e);

        // evict old cache until the cache size is below limit
        while (cache->size > MAX_CACHE_SIZE && cache->head) {
            dbg_printf("Cache entry of %s is evicted\n",
                       cache->head->prev->key);
            evict_entry(cache, cache->head->prev);
        }
    } else {
        remove_entry(cache, e);
        insert_front(cache, e);
    }

    cache_mutex_unlock(cache);
    return (cache_entry_t *)e;
}

cache_entry_t *cache_get(cache_t *_cache, const char *key) {
    _cache_t *cache = (_cache_t *)_cache;
    cache_mutex_lock(cache);

    entry_t *ret = find(cache, key);

    // move to list front
    if (ret) {
        remove_entry(cache, ret);
        insert_front(cache, ret);
        ++ret->ref;
    }

    cache_mutex_unlock(cache);
    return (cache_entry_t *)ret;
}

void cache_entry_release(cache_t *_cache, cache_entry_t *_e) {
    _cache_t *cache = (_cache_t *)_cache;
    cache_mutex_lock(cache);

    entry_t *e = (entry_t *)_e;
    if (--e->ref == 0) {
        free_entry(e);
    }

    cache_mutex_unlock(cache);
}

/**
 * Find entry with key
 * @param cache Cache returned by cache_create
 * @param key String key
 * @return Entry if found, otherwise NULL
 *
 * Not thread-safe
 */
entry_t *find(_cache_t *cache, const char *key) {
    entry_t *curr = cache->head;
    if (!curr) {
        return NULL;
    }

    do {
        if (strcmp(curr->key, key) == 0) {
            return curr;
        }
        curr = curr->next;
    } while (curr && curr != cache->head);

    return NULL;
}

/**
 * Insert entry to the front of the linked list
 *
 * Update the total cache size
 *
 * Not thread-safe
 */
void insert_front(_cache_t *cache, entry_t *e) {
    dbg_assert(e);
    dbg_assert(!e->next);
    dbg_assert(!e->prev);

    entry_t *head = cache->head;
    if (!head) {
        e->prev = e;
        e->next = e;
    } else {
        entry_t *prev = head->prev;
        prev->next = e;
        head->prev = e;

        e->next = head;
        e->prev = prev;
    }

    cache->head = e;
    cache->size += e->size;
}

/**
 * Remove entry from the linked list
 *
 * Update the total cache size
 *
 * Not thread-safe
 */
void remove_entry(_cache_t *cache, entry_t *e) {
    dbg_assert(e);
    dbg_assert(e->next);
    dbg_assert(e->prev);

    if (e->next == e) {
        cache->head = NULL;
    } else {
        entry_t *prev = e->prev;
        entry_t *next = e->next;

        prev->next = next;
        next->prev = prev;
    }

    if (cache->head == e) {
        cache->head = e->next;
    }

    e->prev = NULL;
    e->next = NULL;

    cache->size -= e->size;
}

/**
 * Evict cache entry and free its memory
 *
 * Update the total cache size
 *
 * @see remove_entry
 *
 * Not thread-safe
 */
void evict_entry(_cache_t *cache, entry_t *e) {
    remove_entry(cache, e);
    if (--e->ref == 0) {
        free_entry(e);
    }
}

/**
 * Lock cache mutex
 */
void cache_mutex_lock(_cache_t *cache) {
    if (pthread_mutex_lock(&cache->cache_mutex)) {
        sio_eprintf("Failed to lock cache mutex\n");
        exit(1);
    }
}

/**
 * Unlock cache mutex
 */
void cache_mutex_unlock(_cache_t *cache) {
    if (pthread_mutex_unlock(&cache->cache_mutex)) {
        sio_eprintf("Failed to unlock cache mutex\n");
        exit(1);
    }
}

/**
 * Free cache entry and its content
 */
void free_entry(entry_t *e) {
    Free(e->val);
    Free((void *)e->key);
    Free(e);
}
