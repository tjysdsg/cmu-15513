/**
 * @file mm-splay.c
 * @brief A 64-bit struct-based implicit free list memory allocator
 *
 * 15-213: Introduction to Computer Systems
 *
 * TODO: insert your documentation here. :)
 *
 *************************************************************************
 *
 * ADVICE FOR STUDENTS.
 * - Step 0: Please read the writeup!
 * - Step 1: Write your heap checker.
 * - Step 2: Write contracts / debugging assert statements.
 * - Good luck, and have fun!
 *
 *************************************************************************
 *
 * @author Jiyang Tang <jiyangta@andrew.cmu.edu>
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

#ifdef DEBUG
// #define PRINT_HEAP
// #define PRINT_INSTRUCTION
#endif

/**
 * Above how many times the extra space is relative to the minimum block size
 * do we split the block
 */
#define SPLIT_FACTOR 1.5

/// Use FIFO when inserting a free block to its list if defined, otherwise LIFO
// #define INSERT_POLICY_FIFO

#ifdef DRIVER
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/*
 *****************************************************************************
 * If DEBUG is defined (such as when running mdriver-dbg), these macros      *
 * are enabled. You can use them to print debugging output and to check      *
 * contracts only in debug mode.                                             *
 *                                                                           *
 * Only debugging macros with names beginning "dbg_" are allowed.            *
 * You may not define any other macros having arguments.                     *
 *****************************************************************************
 */
#ifdef DEBUG
#define dbg_requires(expr) assert(expr)
#define dbg_assert(expr) assert(expr)
#define dbg_ensures(expr) assert(expr)
#define dbg_printf(...) ((void)printf(__VA_ARGS__))
#define dbg_printheap(...) print_heap(__VA_ARGS__)
#else
/* When DEBUG is not defined, these should emit no code whatsoever,
 * not even from evaluation of argument expressions.  However,
 * argument expressions should still be syntax-checked and should
 * count as uses of any variables involved.  This used to use a
 * straightforward hack involving sizeof(), but that can sometimes
 * provoke warnings about misuse of sizeof().  I _hope_ that this
 * newer, less straightforward hack will be more robust.  Technically
 * it only works for EXPRs for which (0 && (EXPR)) is valid, but I
 * cannot think of any EXPR usable as a _function parameter_ that
 * doesn't qualify.
 *
 * The "diagnostic push/pop/ignored" pragmas are required to prevent
 * clang from issuing "unused value" warnings about most of the
 * arguments to dbg_printf / dbg_printheap (the argument list is being
 * treated, in this case, as a chain of uses of the comma operator).
 * Yes, these apparently GCC-specific pragmas work with clang,
 * I checked.
 *   -zw 2022-07-15
 */
#define dbg_discard_expr_(expr)                                                \
    (_Pragma("GCC diagnostic push") _Pragma(                                   \
        "GCC diagnostic ignored \"-Wunused-value\"")(void)(0 && (expr))        \
         _Pragma("GCC diagnostic pop"))
#define dbg_requires(expr) dbg_discard_expr_(expr)
#define dbg_assert(expr) dbg_discard_expr_(expr)
#define dbg_ensures(expr) dbg_discard_expr_(expr)
#define dbg_printf(...) dbg_discard_expr_((__VA_ARGS__))
#define dbg_printheap(...) dbg_discard_expr_((__VA_ARGS__))
#endif

/* Basic constants */

typedef uint64_t word_t;

/** @brief Word and header size (bytes) */
static const size_t wsize = sizeof(word_t);

/** @brief Double word size (bytes) */
static const size_t dsize = 2 * wsize;

/**
 * @brief Minimum free block size (bytes)
 *
 * FIXME: the minimum sizes of alloc block and free block are different
 *
 * Header + footer + three pointers + padding
 *
 * @see min_block_size_power
 */
static const size_t min_block_size = 6 * wsize;

/**
 * The minimum size in bytes when extending the heap.
 * (Must be divisible by dsize)
 */
static const size_t chunksize = (1 << 12);

/**
 * Mask of the allocation bit.
 * Since all pointers are aligned by 16 bytes, the last several bits are always
 * 0. We can use these bits for other purposes.
 * The allocation bit indicate whether the block is occupied or free.
 */
static const word_t alloc_mask = 0x1;

/**
 * The size mask is used to clear the last 4 bits to get the size of a block
 */
static const word_t size_mask = ~(word_t)0xF;

/** @brief Represents the header and payload of one block in the heap */
typedef struct block {
    /** @brief Header contains size + allocation flag */
    word_t header;

    union {
        /// @brief A pointer to the block payload.
        char payload[0];
        /// Doubly-linked circular list
        struct {
            struct block *parent;
            struct block *left;
            struct block *right;
        } list;
    };
} block_t;

/**
 * @brief A segregation list
 */
typedef struct seg_list {
    /// lower bound of the block size, inclusive
    word_t low;
    /// upper bound of the block size, exclusive
    word_t high;
    /// start of the list
    block_t *start;
} seg_list_t;

static void tree_init(void);
static void tree_insert(block_t *block);
static block_t *tree_find(block_t *block);
static block_t *tree_find_nearest(size_t size);
static void tree_remove(block_t *block);

/* Global variables */

block_t *tree;

#ifdef DEBUG
/** @brief Pointer to first block heap */
static block_t *heap_start = NULL;
#endif

/*
 * ---------------------------------------------------------------------------
 *                        BEGIN SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Returns the maximum of two integers.
 * @param[in] x
 * @param[in] y
 * @return `x` if `x > y`, and `y` otherwise.
 */
static size_t max(size_t x, size_t y) {
    return (x > y) ? x : y;
}

/**
 * @brief Rounds `size` up to next multiple of n
 * @param[in] size
 * @param[in] n
 * @return The size after rounding up
 */
static size_t round_up(size_t size, size_t n) {
    return n * ((size + (n - 1)) / n);
}

/**
 * @brief Packs the `size` and `alloc` of a block into a word suitable for
 *        use as a packed value.
 *
 * Packed values are used for both headers and footers.
 *
 * The allocation status is packed into the lowest bit of the word.
 *
 * @param[in] size The size of the block being represented
 * @param[in] alloc True if the block is allocated
 * @return The packed value
 */
static word_t pack(size_t size, bool alloc) {
    dbg_assert((size & alloc_mask) == 0);
    return size | alloc;
}

/**
 * @brief Extracts the size represented in a packed word.
 *
 * This function simply clears the lowest 4 bits of the word, as the heap
 * is 16-byte aligned.
 *
 * @param[in] word
 * @return The size of the block represented by the word
 */
static size_t extract_size(word_t word) {
    return (word & size_mask);
}

/**
 * @brief Extracts the size of a block from its header.
 * @param[in] block
 * @return The size of the block
 */
static size_t get_size(block_t *block) {
    return extract_size(block->header);
}

/**
 * @brief Given a payload pointer, returns a pointer to the corresponding
 *        block.
 * @param[in] bp A pointer to a block's payload
 * @return The corresponding block
 */
static block_t *payload_to_header(void *bp) {
    return (block_t *)((char *)bp - offsetof(block_t, payload));
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        payload.
 * @param[in] block
 * @return A pointer to the block's payload
 * @pre The block must be a valid block, not a boundary tag.
 */
static void *header_to_payload(block_t *block) {
    dbg_requires(get_size(block) != 0);
    return (void *)(block->payload);
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        footer.
 * @param[in] block
 * @return A pointer to the block's footer
 * @pre The block must be a valid block, not a boundary tag.
 */
static word_t *header_to_footer(block_t *block) {
    dbg_requires(get_size(block) != 0 &&
                 "Called header_to_footer on the epilogue block");
    // convert payload to (char *) to remove errors raised by
    // UndefinedBehaviorSanitizer
    return (word_t *)((char *)block->payload + get_size(block) - dsize);
}

/**
 * @brief Given a block footer, returns a pointer to the corresponding
 *        header.
 * @param[in] footer A pointer to the block's footer
 * @return A pointer to the start of the block
 * @pre The footer must be the footer of a valid block, not a boundary tag.
 */
static block_t *footer_to_header(word_t *footer) {
    size_t size = extract_size(*footer);
    dbg_assert(size != 0 && "Called footer_to_header on the prologue block");
    return (block_t *)((char *)footer + wsize - size);
}

/**
 * @brief Returns the payload size of a given block.
 *
 * The payload size is equal to the entire block size minus the sizes of the
 * block's header and footer.
 *
 * @param[in] block
 * @return The size of the block's payload
 */
static size_t get_payload_size(block_t *block) {
    size_t asize = get_size(block);
    return asize - dsize;
}

/**
 * @brief Returns the allocation status of a given header value.
 *
 * This is based on the lowest bit of the header value.
 *
 * @param[in] word
 * @return The allocation status corresponding to the word
 */
static bool extract_alloc(word_t word) {
    return (bool)(word & alloc_mask);
}

/**
 * @brief Returns the allocation status of a block, based on its header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_alloc(block_t *block) {
    return extract_alloc(block->header);
}

/**
 * @brief Writes an epilogue header at the given address.
 *
 * The epilogue header has size 0, and is marked as allocated.
 *
 * @param[out] block The location to write the epilogue header
 */
static void write_epilogue(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires((char *)block == (char *)mem_heap_hi() - 7);
    block->header = pack(0, true);
}

/**
 * @brief Writes a block starting at the given address.
 *
 * This function writes both a header and footer, where the location of the
 * footer is computed in relation to the header.
 *
 * TODO: Are there any preconditions or postconditions?
 *
 * @param[out] block The location to begin writing the block header
 * @param[in] size The size of the new block. Must be larger than dsize.
 * @param[in] alloc The allocation status of the new block
 */
static void write_block(block_t *block, size_t size, bool alloc) {
    dbg_requires(block != NULL);
    dbg_requires(size >= min_block_size);

    block->header = pack(size, alloc);
    word_t *footerp = header_to_footer(block);
    *footerp = pack(size, alloc);
}

/**
 * @brief Finds the next consecutive block on the heap.
 *
 * This function accesses the next block in memory by adding the size of
 * the block.
 *
 * @param[in] block A block in the heap
 * @return The next consecutive block on the heap
 * @pre The block is not the epilogue
 */
static block_t *find_next(block_t *block) {
    dbg_requires(block);
    dbg_requires(get_size(block) != 0 &&
                 "Called find_next on the last block in the heap");
    return (block_t *)((char *)block + get_size(block));
}

/**
 * @brief Finds the footer of the previous block on the heap.
 * @param[in] block A block in the heap
 * @return The location of the previous block's footer
 */
static word_t *find_prev_footer(block_t *block) {
    // Compute previous footer position as one word before the header
    return &(block->header) - 1;
}

/**
 * @brief Finds the previous consecutive block on the heap.
 *
 * If the function is called on the first block in the heap, NULL will be
 * returned, since the first block in the heap has no previous block!
 *
 * The position of the previous block is found by reading the previous
 * block's footer to determine its size, then calculating the start of the
 * previous block based on its size.
 *
 * @param[in] block A block in the heap
 * @return The previous consecutive block in the heap.
 */
static block_t *find_prev(block_t *block) {
    dbg_requires(block != NULL);
    word_t *footerp = find_prev_footer(block);

    // Return NULL if called on first block in the heap
    if (extract_size(*footerp) == 0) {
        return NULL;
    }

    return footer_to_header(footerp);
}

/*
 * ---------------------------------------------------------------------------
 *                        END SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] block
 * @return
 */
static block_t *coalesce_block(block_t *block) {
    dbg_requires(block);
    dbg_assert(get_alloc(block) == 0);
    dbg_assert(get_size(block) >= min_block_size);

    // get prev and next block if not epilogue
    word_t *prev_footer = find_prev_footer(block);
    block_t *prev = NULL;
    block_t *next = NULL;
    if (extract_size(*prev_footer))
        prev = footer_to_header(prev_footer);
    if (extract_size(*prev_footer))
        next = find_next(block);

    // prev block is free
    if (prev && !get_alloc(prev)) {
        // remove blocks from the free list
        tree_remove(block);
        tree_remove(prev);

        // merge this and prev
        write_block(prev, get_size(prev) + get_size(block), false);
        block = prev;

        // re-insert the resulting block into the list
        tree_insert(block);
    }

    // next block is free
    if (next && !get_alloc(next)) {
        // remove them from the free list
        tree_remove(next);
        tree_remove(block);

        // merge this and next
        write_block(block, get_size(next) + get_size(block), false);

        // re-insert the resulting block into the list
        tree_insert(block);
    }

    return block;
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] size
 * @return
 */
static block_t *extend_heap(size_t size) {
    void *bp;

    // Allocate an even number of words to maintain alignment
    size = round_up(size, dsize);
    if ((bp = mem_sbrk((intptr_t)size)) == (void *)-1)
        return NULL;

    // Initialize free block header/footer
    block_t *block = payload_to_header(bp);
    write_block(block, size, false);

    // Add block to the free list
    tree_insert(block);

    // Create new epilogue header
    block_t *block_next = find_next(block);
    write_epilogue(block_next);

    // Coalesce in case the previous block was free
    block = coalesce_block(block);

    return block;
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @pre asize must be larger than the min block size
 * @param[in] block
 * @param[in] asize
 */
static void split_block(block_t *block, size_t asize) {
    dbg_requires(get_alloc(block));
    dbg_assert(asize >= min_block_size);
    dbg_assert(get_size(block) >= min_block_size);

    size_t block_size = get_size(block);
    if ((block_size - asize) >= (size_t)(min_block_size * SPLIT_FACTOR)) {
        write_block(block, asize, true);

        block_t *block_next = find_next(block);
        write_block(block_next, block_size - asize, false);
        tree_insert(block_next);
    }

    dbg_ensures(get_alloc(block));
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] asize
 * @return
 */
static block_t *find_fit(size_t asize) {
    return tree_find_nearest(asize);
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] line
 * @return
 */
bool mm_checkheap(int line) {
    // TODO:
    //      block_t *block = heap_start;
    //      size_t size = 0;
    //      while (block && (size = get_size(block))) {
    //          if (get_size(block) < min_free_block_size)
    //              printf("Block size to small (%ld) at 0x%lx (line %d)\n",
    //              size,
    //                     (uintptr_t)block, line);
    //          block = find_next(block);
    //      }

    return true;
}

#ifdef DEBUG
extern void print_heap(void);
void print_heap(void) {
    // TODO: print free tree
    // printf("\n");

    // print heap
    block_t *block = heap_start;
    printf("addr\tsize\talloc\n");
    while (block && get_size(block)) {
        printf("0x%lx\t%zu\t%d\n", (uintptr_t)block, get_size(block),
               get_alloc(block));
        block = find_next(block);
    }
}
#endif

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @return
 */
bool mm_init(void) {
    // reset global variables
    tree_init();
#ifdef DEBUG
    heap_start = NULL;
#endif

    // Create the initial heap containing only the seglist + two epilogue
    word_t *start = (word_t *)(mem_sbrk(2 * wsize));
    memset(start, 0, 2 * wsize);

    if (start == (void *)-1)
        return false;

    start[0] = pack(0, true); // Heap prologue (block footer)
    start[1] = pack(0, true); // Heap epilogue (block header)

#ifdef DEBUG
    // Heap starts with first "block header", currently the epilogue
    heap_start = (block_t *)&(start[1]);
#endif

    // Extend the empty heap with a free block of chunksize bytes
    // extend_heap will set free_list_start to this block
    if (extend_heap(chunksize) == NULL)
        return false;

    return true;
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] size
 * @return
 */
void *malloc(size_t size) {
#ifdef PRINT_INSTRUCTION
    printf("malloc %ld\n", size);
#endif

    dbg_requires(mm_checkheap(__LINE__));

    size_t asize;      // Adjusted block size
    size_t extendsize; // Amount to extend heap if no fit is found
    block_t *block;
    void *bp = NULL;

    // Ignore spurious request
    if (size == 0) {
        dbg_ensures(mm_checkheap(__LINE__));
        return bp;
    }

    // Adjust block size to include overhead and to meet alignment requirements
    asize = round_up(size + dsize, dsize);
    asize = max(asize, min_block_size);

    // Search the free list for a fit
    block = find_fit(asize);

    // If no fit is found, request more memory, and then and place the block
    if (block == NULL) {
        // Always request at least chunksize
        extendsize = max(asize, chunksize);
        block = extend_heap(extendsize);
        // extend_heap returns an error
        if (block == NULL)
            return bp;
    }

    // The block should be marked as free
    dbg_assert(!get_alloc(block));

    // Mark block as allocated
    tree_remove(block);
    size_t block_size = get_size(block);
    write_block(block, block_size, true);

    // Try to split the block if too large
    split_block(block, asize);

    bp = header_to_payload(block);

    dbg_ensures(mm_checkheap(__LINE__));

#ifdef PRINT_HEAP
    printf("===========================\n");
    print_heap();
    printf("\n");
#endif
    return bp;
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] bp
 */
void free(void *bp) {
#ifdef PRINT_INSTRUCTION
    printf("free 0x%lx\n", (uintptr_t)bp);
#endif

    dbg_requires(mm_checkheap(__LINE__));

    if (bp == NULL)
        return;

    block_t *block = payload_to_header(bp);
    size_t size = get_size(block);

    // The block should be marked as allocated
    dbg_assert(get_alloc(block));

    // Mark the block as free
    write_block(block, size, false);
    tree_insert(block);

    // Try to coalesce the block with its neighbors
    coalesce_block(block);

    dbg_ensures(mm_checkheap(__LINE__));
#ifdef PRINT_HEAP
    printf("===========================\n");
    print_heap();
    printf("\n");
#endif
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] ptr
 * @param[in] size
 * @return
 */
void *realloc(void *ptr, size_t size) {
    block_t *block = payload_to_header(ptr);
    size_t copysize;
    void *newptr;

    // If size == 0, then free block and return NULL
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    // If ptr is NULL, then equivalent to malloc
    if (ptr == NULL)
        return malloc(size);

    // Don't allocate new memory if new size is smaller than or equal to
    // the original size
    size_t asize = round_up(size + dsize, dsize);
    asize = max(asize, min_block_size);
    if (asize <= get_size(block)) {
        // split if enough space for a new free block
        // TODO: split only when the extra space > some margin?
        if (get_size(block) - asize > min_block_size)
            split_block(block, asize);
        return ptr;
    }

    // Otherwise, proceed with reallocation
    newptr = malloc(size);

    // If malloc fails, the original block is left untouched
    if (newptr == NULL) {
        return NULL;
    }

    // Copy the old data
    copysize = get_payload_size(block); // gets size of old payload
    if (size < copysize) {
        copysize = size;
    }
    memcpy(newptr, ptr, copysize);

    // Free the old block
    free(ptr);

    return newptr;
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] elements
 * @param[in] size
 * @return
 */
void *calloc(size_t elements, size_t size) {
    void *bp;
    size_t asize = elements * size;

    if (elements == 0) {
        return NULL;
    }
    if (asize / elements != size) {
        // Multiplication overflowed
        return NULL;
    }

    bp = malloc(asize);
    if (bp == NULL) {
        return NULL;
    }

    // Initialize all bits to 0
    memset(bp, 0, asize);

    return bp;
}

/*
 * ---------------------------------------------------------------------------
 *                        TREE-RELATED FUNCTIONS
 * ---------------------------------------------------------------------------
 */

static void left_rotate(block_t *x) {
    block_t *y = x->list.right;
    if (y) {
        x->list.right = y->list.left;
        if (y->list.left)
            y->list.left->list.parent = x;
        y->list.parent = x->list.parent;
    }
    if (!x->list.parent)
        tree = y;
    else if (x == x->list.parent->list.left)
        x->list.parent->list.left = y;
    else
        x->list.parent->list.right = y;
    if (y)
        y->list.left = x;
    x->list.parent = y;
}

static void right_rotate(block_t *x) {
    block_t *y = x->list.left;
    if (y) {
        x->list.left = y->list.right;
        if (y->list.right)
            y->list.right->list.parent = x;
        y->list.parent = x->list.parent;
    }
    if (!x->list.parent)
        tree = y;
    else if (x == x->list.parent->list.left)
        x->list.parent->list.left = y;
    else
        x->list.parent->list.right = y;
    if (y)
        y->list.right = x;
    x->list.parent = y;
}

static void splay(block_t *x) {
    while (x->list.parent) {
        if (!x->list.parent->list.parent) {
            if (x->list.parent->list.left == x)
                right_rotate(x->list.parent);
            else
                left_rotate(x->list.parent);
        } else if (x->list.parent->list.left == x &&
                   x->list.parent->list.parent->list.left == x->list.parent) {
            right_rotate(x->list.parent->list.parent);
            right_rotate(x->list.parent);
        } else if (x->list.parent->list.right == x &&
                   x->list.parent->list.parent->list.right == x->list.parent) {
            left_rotate(x->list.parent->list.parent);
            left_rotate(x->list.parent);
        } else if (x->list.parent->list.left == x &&
                   x->list.parent->list.parent->list.right == x->list.parent) {
            right_rotate(x->list.parent);
            left_rotate(x->list.parent);
        } else {
            left_rotate(x->list.parent);
            right_rotate(x->list.parent);
        }
    }
}

static void replace(block_t *u, block_t *v) {
    if (!u->list.parent)
        tree = v;
    else if (u == u->list.parent->list.left)
        u->list.parent->list.left = v;
    else
        u->list.parent->list.right = v;
    if (v)
        v->list.parent = u->list.parent;
}

static block_t *subtree_minimum(block_t *u) {
    while (u->list.left)
        u = u->list.left;
    return u;
}

static block_t *subtree_maximum(block_t *u) {
    while (u->list.right)
        u = u->list.right;
    return u;
}

void tree_init(void) {
    tree = NULL;
}

void tree_insert(block_t *block) {
    dbg_assert(!get_alloc(block));
    size_t key = get_size(block);
    block_t *p = NULL;
    block_t *tmp = tree;
    while (tmp) {
        p = tmp;
        if (key > get_size(tmp))
            tmp = tmp->list.right;
        else
            tmp = tmp->list.left;
    }

    block->list.parent = p;
    block->list.left = block->list.right = NULL;
    if (!p)
        tree = block;
    else if (get_size(p) < key)
        p->list.right = block;
    else
        p->list.left = block;
    splay(block);
    return;
}

block_t *tree_find_nearest(size_t key) {
    block_t *z = tree;
    block_t *n = NULL;
    while (z) {
        if (key == get_size(z))
            return z;
        if (key < get_size(z)) {
            if (!n || get_size(n) > get_size(z))
                n = z;
            z = z->list.left;
        } else
            z = z->list.right;
    }
    return n;
}

void tree_remove(block_t *block) {
    dbg_assert(!get_alloc(block));

    splay(block);
    if (!block->list.left)
        replace(block, block->list.right);
    else if (!block->list.right)
        replace(block, block->list.left);
    else {
        block_t *y = subtree_minimum(block->list.right);
        if (y->list.parent != block) {
            replace(y, y->list.right);
            y->list.right = block->list.right;
            y->list.right->list.parent = y;
        }
        replace(block, y);
        y->list.left = block->list.left;
        y->list.left->list.parent = y;
    }
}

/*
 *****************************************************************************
 * Do not delete the following super-secret(tm) lines!                       *
 *                                                                           *
 * 53 6f 20 79 6f 75 27 72 65 20 74 72 79 69 6e 67 20 74 6f 20               *
 *                                                                           *
 * 66 69 67 75 72 65 20 6f 75 74 20 77 68 61 74 20 74 68 65 20               *
 * 68 65 78 61 64 65 63 69 6d 61 6c 20 64 69 67 69 74 73 20 64               *
 * 6f 2e 2e 2e 20 68 61 68 61 68 61 21 20 41 53 43 49 49 20 69               *
 *                                                                           *
 * 73 6e 27 74 20 74 68 65 20 72 69 67 68 74 20 65 6e 63 6f 64               *
 * 69 6e 67 21 20 4e 69 63 65 20 74 72 79 2c 20 74 68 6f 75 67               *
 * 68 21 20 2d 44 72 2e 20 45 76 69 6c 0a c5 7c fc 80 6e 57 0a               *
 *                                                                           *
 *****************************************************************************
 */
