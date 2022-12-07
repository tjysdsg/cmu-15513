/**
 * @file mm.c
 * @brief A 64-bit struct-based implicit free list memory allocator
 *
 * 15-213: Introduction to Computer Systems
 *
 * This dynamic memory allocator uses segregation free lists,
 * better-fit searching, and LIFO insertion policy.
 *
 * The heap layout is as following:
 * -----------------------------------------------------------------
 * | seg lists | prologue |        memory blocks        | epilogue |
 * -----------------------------------------------------------------
 *
 * An allocated memory block is structured as:
 * |-----------------------------|
 * | size  | prev_alloc  | alloc |  <- header
 * |-----------------------------|
 * |                             |
 * |       payload               |
 * |                             |
 * |-----------------------------|
 *
 * A free block is structured as:
 * |-----------------------------|
 * | size  | prev_alloc  | alloc |  <- header
 * |-----------------------------|
 * |   ptr to next free block    |
 * |-----------------------------|
 * |    ptr to prev free block   |
 * |-----------------------------|
 * |                             |
 * |                             |
 * |                             |
 * |-----------------------------|
 * | size  | prev_alloc  | alloc |  <- footer
 * |-----------------------------|
 *
 * The alloc bit indicates if the block is allocated or free. The prev_alloc
 * bit indicates if the previous consecutive block is allocated (since we
 * eliminated the footer in allocated blocks).
 *
 * There are also miniblocks that are 16-byte in size.
 * |-------------------------------------------|
 * | size     | is_mini  | prev_alloc  | alloc |  <- header
 * |-------------------------------------------|
 * | next ptr | is_mini  | prev_alloc  | alloc |  <- payload if allocated
 * |-------------------------------------------|
 * They cannot hold all components of a regular free blocks but have lower
 * overhead for 8-byte payloads. We reuse the lower 3-bits of the header and
 * the next pointer (since the pointers are 16-byte aligned) to store 3 status
 * bits. The prev_alloc and alloc are the same as regular blocks, but is_mini is
 * added to indicate if the block is a miniblock. The next pointer is reused
 * as the 8-byte payload if the block is allocated.
 *
 * The prologue and epilogue are size-0 block (marked as allocated) that denote
 * the start and the end of the memory blocks.
 *
 * The segregation lists are grouped lists that keep records of all free blocks.
 * Every group has a lower and an upper bound of the block size. All blocks
 * whose sizes are within this interval are stored in the corresponding lists.
 * The group sizes are [2^4, 2^5), [2^5, 2^6), ..., [2^14, infinity).
 * See get_seg_list().
 *
 * When malloc() is called, a free block with enough size is searched linearly
 * within a group. Extra size of this block is used as a new free block.
 * If no available block is found, we extend the heap. Instead of extending the
 * heap by a fixed size (chunksize), the amount to extend is 4 times the block
 * size.
 *
 * When free() is called, the block is marked as free. And if the previous or
 * next consecutive block is/are also free, they are merged as a single free
 * block and added to the seglist.
 *
 * Each list in a group is a circular doubly-linked list. New blocks are
 * inserted in a LIFO order.
 *
 * @see mm_malloc
 * @see mm_free
 * @see mm_realloc
 * @see mm_calloc
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
#include <strings.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

#ifdef DEBUG
/**
 * Print the entire heap at the end of malloc() and free() if true
 */
// #define PRINT_HEAP

/**
 * Print the trace being executed at the end of malloc() and free() if true
 */
// #define PRINT_INSTRUCTION
#endif

/**
 * Number of extra blocks to search once the first available is found
 */
#define BETTER_FIT_LIMIT 20

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
 * @brief Minimum free block size (bytes, excluding miniblocks)
 *
 * @note The minimum sizes of alloc block and free block are different, but
 *       we use the the min size of free block as the overall min size
 *
 * Header + footer + two pointers
 */
static const size_t min_block_size = 4 * wsize;

/**
 * @brief Size of a miniblock size (bytes)
 *
 * Header + the next pointer
 */
static const size_t miniblock_size = dsize;

/**
 * Number of segregation lists
 *
 * @see get_seg_list
 */
static const size_t n_segs = 13;

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
 * Mask of the previous allocation bit.
 * @see alloc_mask
 */
static const word_t prev_alloc_mask = 0x2;

/**
 * Mask of the mini bit.
 *
 * The mini bit indicate whether the block is a miniblock.
 */
static const word_t mini_mask = 0x4;

/**
 * The size mask is used to clear the last 4 bits to get the size of a block
 */
static const word_t size_mask = ~(word_t)0xF;

/**
 * The pointer mask is used to clear the last 3 bits to get the next pointer of
 * a block
 */
static const word_t miniblock_ptr_mask = ~(word_t)0x7;

/**
 * @brief Represents the header and payload of one block in the heap
 */
typedef struct block {
    /// Header contains size + prev alloc bit + alloc bit
    word_t header;

    union {
        /// A pointer to the block payload.
        char payload[0];
        /// Doubly-linked circular list
        struct {
            struct block *next;
            struct block *prev;
        } list;
    };
} block_t;

/**
 * @brief A segregation list
 */
typedef struct seg_list {
    /// start of the list
    block_t *start;
} seg_list_t;

/* Global variables */

/**
 * @brief Segregation lists used in this implementation
 *
 * @see get_seg_list
 * @see mm_init
 */
static seg_list_t *seg_lists = NULL;

#ifdef DEBUG
/**
 * Pointer to first block (not prologue/epilogue) on the heap
 */
static block_t *heap_start = NULL;

/**
 * @brief Heap size in bytes
 *
 * @note Not including the seglist
 */
static size_t heap_size = 0;
#endif

/*
 * ---------------------------------------------------------------------------
 *                        BEGIN SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * Wraps mem_sbrk with debug utilities (if enabled)
 * @param incr Size to extend
 * @see sbrk
 * @see mem_sbrk
 */
static void *sbrk_wrapper(size_t incr) {
    void *ret = mem_sbrk((intptr_t)incr);

#ifdef DEBUG
    heap_size += (size_t)incr;
#endif

    return ret;
}

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
 * @param[in] alloc Two bits. The first bit is set if the block is allocated.
 *       The second bit is set if the previous consecutive block is allocated.
 * @return The packed value
 */
static word_t pack(size_t size, size_t alloc) {
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
    // miniblock
    if (word & mini_mask)
        return miniblock_size;
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
 * @brief Returns the allocation status of a block, based on its header.
 *
 * This is based on the lowest bit of the header value.
 *
 * @param[in] block
 * @return The allocation status of the block
 */
static bool extract_alloc(word_t header) {
    return (bool)(header & alloc_mask);
}

/**
 * @brief Returns the allocation status of a block, based on its header.
 *
 * This is based on the lowest bit of the header value.
 *
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_alloc(block_t *block) {
    return extract_alloc(block->header);
}

/**
 * Check if the previous consecutive block is allocated (the second alloc bit)
 *
 * This is based on the second lowest bit of the header value.
 *
 * @param block A block on the heap
 * @return The allocation status of the previous consecutive block
 */
static bool get_prev_alloc(block_t *block) {
    return (bool)(block->header & prev_alloc_mask);
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
    dbg_requires(get_size(block) >= min_block_size &&
                 "Miniblocks cannot have a footer");
    dbg_requires(!get_alloc(block) && "Alloc block cannot have a footer");
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
 * block's header (alloc block has no footer)
 *
 * @param[in] block
 * @return The size of the block's payload
 */
static size_t get_payload_size(block_t *block) {
    size_t asize = get_size(block);
    return asize - wsize;
}

/**
 * @brief Writes an epilogue header at the given address.
 *
 * The epilogue header has size 0, and is marked as allocated.
 *
 * @param[out] block The location to write the epilogue header
 */
static void write_epilogue(block_t *block, bool prev_alloc) {
    dbg_requires(block != NULL);
    block->header = pack(0, ((size_t)prev_alloc << 1) | 1);
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
    if (extract_size(*footerp) == 0)
        return NULL;

    return footer_to_header(footerp);
}

/**
 * Get the next pointer of miniblock by removing status bits
 * @param block A miniblock
 * @return The next pointer
 */
static block_t *get_miniblock_next_pointer(block_t *block) {
    return (block_t *)((uintptr_t)block->list.next & miniblock_ptr_mask);
}

/**
 * Set miniblock's next pointer without changing the status bits
 * @param block A miniblock
 * @param val The pointer to the next block
 */
static void set_miniblock_next_pointer(block_t *block, block_t *val) {
    uintptr_t status_bits = (uintptr_t)block->list.next & ~miniblock_ptr_mask;
    block->list.next = (block_t *)((uintptr_t)val | status_bits);
}

/**
 * @brief Writes a block starting at the given address.
 *
 * This function writes both a header and footer, where the location of the
 * footer is computed in relation to the header.
 *
 * In addition, it sets the prev_alloc bit of the next consecutive block
 * according to the value of alloc.
 *
 * @param[out] block The location to begin writing the block header
 * @param[in] size The size of the new block. Must be larger than dsize.
 * @param[in] prev_alloc The allocation status of the previous consecutive block
 * @param[in] alloc The allocation status of the new block
 *
 * @post The next and prev pointers are kept as is
 */
static void write_block(block_t *block, size_t size, bool prev_alloc,
                        bool alloc) {
    dbg_requires(block != NULL);

    size_t bits = ((size_t)prev_alloc << 1) | (size_t)alloc;

    // miniblock has the mini bit set
    if (size < min_block_size)
        bits |= mini_mask;

    block->header = pack(size, bits);

    // allocated blocks do not have a footer
    // miniblocks don't have a footer
    if (!alloc && size >= min_block_size) {
        word_t *footerp = header_to_footer(block);
        *footerp = pack(size, bits);
    }

    // miniblock has the mini bit of the next pointer set
    if (!alloc && size < min_block_size) {
        uintptr_t ptr = (uintptr_t)get_miniblock_next_pointer(block);
        ptr |= bits;
        block->list.next = (block_t *)ptr;
    }
}

/**
 * Set the prev_alloc bit of the next consecutive block (if it's not epilogue)
 * @param block A block on the heap
 */
static void set_next_prev_alloc(block_t *block) {
    dbg_assert(get_size(block));

    block_t *next = find_next(block);
    if (get_size(next))
        write_block(next, get_size(next), get_alloc(block), get_alloc(next));
    else
        write_epilogue(next, get_alloc(block));
}

/**
 * @brief Choose which segregation list to use according to the block size
 * @param size Size in bytes
 * @return Suitable segregation list. NULL if not found.
 */
static seg_list_t *get_seg_list(size_t size) {
    if (size >= 16 && size < 32) {
        return seg_lists;
    } else if (size >= 32 && size < 48) {
        return seg_lists + 1;
    } else if (size >= 48 && size < 64) {
        return seg_lists + 2;
    } else if (size >= 64 && size < 96) {
        return seg_lists + 3;
    } else if (size >= 96 && size < 128) {
        return seg_lists + 4;
    } else if (size >= 128 && size < 256) {
        return seg_lists + 5;
    } else if (size >= 256 && size < 384) {
        return seg_lists + 6;
    } else if (size >= 384 && size < 512) {
        return seg_lists + 7;
    } else if (size >= 512 && size < 1024) {
        return seg_lists + 8;
    } else if (size >= 1024 && size < 2048) {
        return seg_lists + 9;
    } else if (size >= 2048 && size < 4096) {
        return seg_lists + 10;
    } else if (size >= 4096 && size < 8192) {
        return seg_lists + 11;
    }
    return seg_lists + 12;
}

/**
 * @brief Add a block to the explicit free list
 *
 * LIFO
 *
 * @param block A free block in the heap
 */
static void add_block_to_free_list(block_t *block) {
    dbg_assert(get_alloc(block) == 0);

    seg_list_t *list = get_seg_list(get_size(block));
    dbg_assert(list);
    dbg_assert(block != list->start);

    // miniblock
    if (get_size(block) < min_block_size) {
        // if list is empty
        if (!list->start) {
            list->start = block;
            set_miniblock_next_pointer(block, block);
            return;
        }

        block_t *prev = list->start;
        block_t *next = get_miniblock_next_pointer(list->start);
        set_miniblock_next_pointer(prev, block);
        set_miniblock_next_pointer(block, next);
        return;
    }

    // if list is empty
    if (!list->start) {
        list->start = block;
        block->list.prev = block;
        block->list.next = block;
        return;
    }

    block_t *prev = list->start->list.prev;
    block->list.prev = prev;
    block->list.next = list->start;
    list->start->list.prev = block;
    prev->list.next = block;
    list->start = block;
}

/**
 * @brief Remove a block from the explicit free list
 *
 * @param block A free block in the list
 *
 * @pre The block must be already on a free list
 * @post The next and prev field are all set to NULL
 */
static void remove_block_from_free_list(block_t *block) {
    dbg_assert(block);
    dbg_assert(get_alloc(block) == 0);

    seg_list_t *list = get_seg_list(get_size(block));
    dbg_assert(list);

    block_t *next = block->list.next;
    if (get_size(block) < min_block_size)
        next = get_miniblock_next_pointer(block);
    dbg_assert(next);

    // this block is the only block on the list
    if (next == block) {
        list->start = NULL;
    } else {
        // miniblock
        if (get_size(block) < min_block_size) {
            block_t *prev = block;
            while (get_miniblock_next_pointer(prev) != block)
                prev = get_miniblock_next_pointer(prev);

            set_miniblock_next_pointer(prev, next);
        } else {
            dbg_assert(block->list.prev);
            block_t *prev = block->list.prev;

            prev->list.next = next;
            next->list.prev = prev;
        }
    }

    // update list head if this block is the first free block
    if (list->start == block)
        list->start = next;

    // clear next and prev
    if (get_size(block) >= min_block_size) {
        block->list.prev = NULL;
        block->list.next = NULL;
    } else {
        set_miniblock_next_pointer(block, NULL);
    }
}

/*
 * Debug utilities
 */
#ifdef DEBUG
/**
 * Get a pointer to the end of the heap
 * (the return value points to the epilogue)
 */
static inline block_t *get_heap_end() {
    return (block_t *)((char *)heap_start + heap_size);
}

/**
 * Print the entire heap, including the pointers in the explicit free lists and
 * every memory block
 *
 * @details 'extern' is used to allow calling this function in GDB
 */
extern void print_heap(void);
void print_heap(void) {
    // print seg lists
    size_t lower_bound = 16;
    printf("lower\tstart\n");
    for (size_t i = 0; i < n_segs; ++i) {
        printf("%zu\t0x%lx\n", lower_bound, (uintptr_t)seg_lists[i].start);
        lower_bound *= 2;
    }

    printf("\n");

    // print heap
    block_t *block = heap_start;
    printf("address\tsize\tprev_alloc\talloc\tnext\tprev\n");
    while (block && block < get_heap_end()) {
        printf("0x%lx\t%zu\t%d\t%d", (uintptr_t)block, get_size(block),
               get_prev_alloc(block), get_alloc(block));

        if (!get_alloc(block))
            printf("\t0x%lx\t0x%lx\n",
                   (uintptr_t)get_miniblock_next_pointer(block),
                   (uintptr_t)block->list.prev);
        else
            printf("\t\t\n");

        block = find_next(block);
    }
    printf("0x%lx\tepilogue\t%d\t\t\t\t\n", (uintptr_t)block,
           get_prev_alloc(block));
}
#endif

/*
 * ---------------------------------------------------------------------------
 *                        END SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Merge consecutive free blocks around the current block
 *
 * @pre block must be free
 * @post returned block is a free block
 *
 * @param[in] block A free block in the heap
 * @return The merged block
 */
static block_t *coalesce_block(block_t *block) {
    dbg_requires(block);
    dbg_assert(get_alloc(block) == 0);
    dbg_assert(get_size(block) >= miniblock_size);

    // get prev and next block if not prologue/epilogue
    block_t *prev = NULL;
    bool prev_free = !get_prev_alloc(block);
    if (prev_free) // prologue has alloc set to true
        prev = find_prev(block);

    block_t *next = NULL;
    next = find_next(block);
    bool next_free = next && !get_alloc(next);

    // the reason that we re-insert the block is that the resulting size may
    // change, thus moving them to another free list

    if (prev_free && !next_free) { // prev free, next not
        remove_block_from_free_list(prev);

        write_block(prev, get_size(prev) + get_size(block),
                    get_prev_alloc(prev), false);
        block = prev;
    } else if (!prev_free && next_free) { // next free, prev not
        remove_block_from_free_list(next);

        write_block(block, get_size(next) + get_size(block),
                    get_prev_alloc(block), false);
    } else if (prev_free && next_free) { // both free
        remove_block_from_free_list(next);
        remove_block_from_free_list(prev);

        write_block(prev, get_size(next) + get_size(prev) + get_size(block),
                    get_prev_alloc(prev), false);
        block = prev;
    }

    return block;
}

/**
 * @brief Merge a free block and its next consecutive free block if possible
 *
 * The only difference between coalesce_block and this function is that this
 * doesn't care about the previous block, thus slightly faster.
 *
 * @see coalesce_block
 *
 * @pre block must be free
 * @post returned block is a free block
 *
 * @param[in] block A free block in the heap
 * @return The merged block
 */
static block_t *coalesce_next(block_t *block) {
    dbg_requires(block);
    dbg_assert(get_alloc(block) == 0);
    dbg_assert(get_size(block) >= miniblock_size);

    // get next block if not epilogue
    block_t *next = NULL;
    next = find_next(block);
    if (next && !get_alloc(next)) {
        remove_block_from_free_list(next);
        write_block(block, get_size(next) + get_size(block),
                    get_prev_alloc(block), false);
    }
    return block;
}

/**
 * @brief Merge a free block and its prev consecutive free block if possible
 *
 * The only difference between coalesce_block and this function is that this
 * doesn't care about the next block, thus slightly faster.
 *
 * @see coalesce_block
 *
 * @pre block must be free
 * @post returned block is a free block
 *
 * @param[in] block A free block in the heap
 * @return The merged block
 */
static block_t *coalesce_prev(block_t *block) {
    dbg_requires(block);
    dbg_assert(get_alloc(block) == 0);
    dbg_assert(get_size(block) >= miniblock_size);

    block_t *prev = NULL;
    bool prev_free = !get_prev_alloc(block);
    if (prev_free) // prologue has alloc set to true
        prev = find_prev(block);

    if (prev_free) {
        remove_block_from_free_list(prev);

        write_block(prev, get_size(prev) + get_size(block),
                    get_prev_alloc(prev), false);
        block = prev;
    }
    return block;
}

/**
 * @brief Extend the heap
 *
 * The size increment will be scaled up to the multiple of dsize.
 *
 * The newly acquired memory block is automatically added to its corresponding
 * seglist
 *
 * @see dsize
 *
 * @param[in] size The size increment
 * @return The the newly acquired free memory as a block. NULL if failed.
 */
static block_t *extend_heap(size_t size) {
    void *bp;

    // Allocate an even number of words to maintain alignment
    size = round_up(size, dsize);
    if ((bp = sbrk_wrapper(size)) == (void *)-1)
        return NULL;

    // Initialize the new free block
    block_t *block = payload_to_header(bp); // the old epilogue right now
    write_block(block, size, get_prev_alloc(block), false);

    // Create the new epilogue
    block_t *new_epilogue = find_next(block);
    write_epilogue(new_epilogue, false);

    block = coalesce_prev(block);
    add_block_to_free_list(block);
    return block;
}

/**
 * @brief Split an alloc block into an alloc block and a free block, depending
 * on the size of the payload
 *
 * If asize is not larger than or equal to miniblock_size, no splitting is
 * performed
 *
 * @pre block is a valid alloc block
 * @pre asize is larger than min_block_size
 *
 * @see miniblock_size
 *
 * @param[in] block An alloc block in the heap
 * @param[in] asize The size of the minimum alloc block
 */
static void split_block(block_t *block, size_t asize) {
    dbg_requires(get_alloc(block));
    dbg_assert(get_size(block) >= asize);

    size_t block_size = get_size(block);
    if ((block_size - asize) >= miniblock_size) {
        write_block(block, asize, get_prev_alloc(block), true);

        block_t *next = find_next(block);
        write_block(next, block_size - asize, true, false);
        set_next_prev_alloc(next);

        block = coalesce_next(next);
        add_block_to_free_list(next);
    }

    dbg_ensures(get_alloc(block));
}

/**
 * Try to find the smallest available free block which has the least size that
 * is >= asize
 *
 * The function fallbacks to the lists with larger sizes if no available
 * candidate is found in the corresponding list.
 *
 * This is a better-fit search. The number of extra blocks to search is set by
 * BETTER_FIT_LIMIT.
 *
 * If no block is found, returns NULL.
 *
 * @param[in] asize Minimum required block size in bytes
 * @return The block if found.
 */
static block_t *find_fit(size_t asize) {
    seg_list_t *list = get_seg_list(asize);
    dbg_assert(list);

    block_t *ret = NULL;
    size_t best_size = INT64_MAX;

    // fallback to lists with larger sizes if cannot find an available block
    // in the current list
    size_t n_search = 0;
    while (list < seg_lists + n_segs) {
        block_t *block = list->start;
        while (block) {
            size_t block_size = get_size(block);
            if (asize <= block_size && best_size > block_size) {
                best_size = block_size;
                ret = block;
            }

            // search BETTER_FIT_LIMIT extra blocks after the first candidate is
            // found
            if (ret)
                ++n_search;

            if (block_size == asize ||
                n_search >= BETTER_FIT_LIMIT) // best found or end of search
                break;

            if (block_size == miniblock_size)
                block = get_miniblock_next_pointer(block);
            else
                block = block->list.next;

            if (block == list->start) // current list is over
                break;
        }

        if (ret) // early stop if already found
            break;
        ++list;
    }
    return ret;
}

/**
 * @brief Check the entire heap for any error in blocks or seglists.
 *
 * 1. Check if all block_t pointers are valid
 * 2. Check if any block is outside of the heap
 * 3. Check if the heap ends (epilogue) prematurely
 * 4. Check if any block's size is too small (< min_block_size)
 * 5. Check if every free block is in its corresponding seglist
 * 6. Check if two consecutive free blocks are coalesced
 * 7. Check if all miniblocks have their mini bit set
 * 8. Check prev_alloc bit
 *
 * This is a debug utility, nothing is done in release mode.
 *
 * @param[in] line The line number of this function's caller
 * @return True if the entire heap is valid
 */
bool mm_checkheap(int line) {
#ifdef DEBUG
    block_t *block = heap_start;
    bool prev_free = false;
    while (true) {
        if (!block) {
            printf("checkheap: encountered nullptr at 0x%lx\n",
                   (uintptr_t)block);
            return false;
        }

        if (block > get_heap_end()) {
            printf("checkheap: block is outside of heap (0x%lx)\n",
                   (uintptr_t)block);
            return false;
        }

        size_t size = get_size(block);
        if (size == 0) {
            // check if epilogue is at the end of heap
            if ((uintptr_t)block == (uintptr_t)get_heap_end()) {
                break;
            } else {
                printf("checkheap: unexpected block with size 0 (0x%lx)\n",
                       (uintptr_t)block);
                return false;
            }
        }

        if (size < miniblock_size) {
            printf("checkheap: Block size too small (%ld) at 0x%lx", size,
                   (uintptr_t)block);
            return false;
        }
        if (size < min_block_size && !(block->header & mini_mask)) {
            printf("checkheap: Miniblock's status bit is not set at 0x%lx",
                   (uintptr_t)block);
            return false;
        }

        if (prev_free && get_prev_alloc(block)) {
            printf("Incorrect prev_alloc at 0x%lx\n", (uintptr_t)block);
            return false;
        }

        // if the block is free, check if it is in the free list
        bool alloc = get_alloc(block);
        if (!alloc) {
            if (prev_free) {
                printf("Two consecutive free blocks are not coalesced. The "
                       "second block is at 0x%lx\n",
                       (uintptr_t)block);
                return false;
            }

            seg_list_t *list = get_seg_list(size);
            if (!list) {
                printf("Cannot find a seglist for block size %ld\n",
                       get_size(block));
                return false;
            }

            block_t *curr = list->start;
            bool found = false;
            while (curr) {
                if (block == curr) {
                    found = true;
                    break;
                }
                curr = get_miniblock_next_pointer(curr);
                if (curr == list->start) // the end of the circular list
                    break;
            }

            if (!found)
                printf("Cannot find the free block at 0x%lx in the seglist "
                       "(size=%ld)\n",
                       (uintptr_t)block, get_size(block));
        }

        prev_free = !alloc;
        block = find_next(block);
    }
    return true;
#else
    /* // Print the longest seglist
    size_t max_n = 0;
    size_t idx = 0;
    for (size_t i = 0; i < n_segs; ++i) {
        size_t n = 0;
        seg_list_t *list = seg_lists + i;

        if (!list->start)
            continue;

        block_t *block = get_miniblock_next_pointer(list->start);
        while (block != list->start) {
            ++n;
            block = get_miniblock_next_pointer(block);
        }

        if (n > max_n) {
            max_n = n;
            idx = i;
        }
    }
    if (max_n > 50)
        printf("seg %ld: %ld\n", 1UL << idx, max_n);
    */
    return true;
#endif
}

/**
 * Initialize segregation lists
 * @param start Starting address of the seg list
 */
static void init_seg_lists(void *start) {
    seg_lists = (seg_list_t *)start;
    for (size_t i = 0; i < n_segs; ++i) {
        seg_lists[i].start = NULL;
    }
}

/**
 * @brief Init the heap, and the global variables used by *alloc/free
 *
 * 1. Initializes the heap and create the prologue and epilogue
 * 2. Initializes seglists
 * 3. Resets all global variables
 *
 * @return True if success
 */
bool mm_init(void) {
    // reset global variables
    seg_lists = NULL;

    size_t seg_size = round_up(sizeof(seg_list_t) * n_segs, dsize);
    word_t *start = (word_t *)(sbrk_wrapper(2 * wsize + seg_size));
    memset(start, 0, 2 * wsize + seg_size);

    if (start == (void *)-1)
        return false;

    init_seg_lists(start);

    // prologue/epilogue
    start = (word_t *)((char *)start + seg_size);
    write_epilogue((block_t *)start, true);
    write_epilogue((block_t *)(start + 1), true);

#ifdef DEBUG
    // Heap starts with first "block header", currently the epilogue
    heap_start = (block_t *)(start + 1);
    heap_size = 0; // not including the seglist
#endif

    return true;
}

/**
 * @brief Allocate a block of memory that can contain size bytes of data
 *
 * The overhead size is added to size, and the result is rounded up to the
 * multiple of dsize that is larger than miniblock_size.
 *
 * Returns NULL if failed
 *
 * @see min_block_size
 * @see miniblock_size
 *
 * @param[in] size Size in bytes
 * @return The pointer to the allocated memory payload
 */
void *malloc(size_t size) {
#ifdef PRINT_INSTRUCTION
    printf("malloc %ld\n", size);
#endif

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
    asize = round_up(size + wsize, dsize);

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
    remove_block_from_free_list(block);
    size_t block_size = get_size(block);
    write_block(block, block_size, get_prev_alloc(block), true);
    set_next_prev_alloc(block);

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
 * @brief Free a previously allocated memory block
 * @param[in] bp The pointer to the memory payload
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
    write_block(block, size, get_prev_alloc(block), false);
    set_next_prev_alloc(block);

    // Try to coalesce the block with its neighbors
    block = coalesce_block(block);

    add_block_to_free_list(block);

    dbg_ensures(mm_checkheap(__LINE__));
#ifdef PRINT_HEAP
    printf("===========================\n");
    print_heap();
    printf("\n");
#endif
}

/**
 * @brief Extend/shrink a block of memory with its content preserved
 *
 * If the size is larger than the previous size, a new memory block is acquired
 * and the previous content is copied there.
 * If the size is smaller than the previous size, the previous memory block is
 * reused.
 *
 * @param[in] ptr Previous pointer returned by *alloc
 * @param[in] size New size
 * @return The new memory block
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
    size_t asize = round_up(size + wsize, dsize);
    if (asize <= get_size(block)) {
        // split if enough space for a new free block
        split_block(block, asize);
        return ptr;
    }

    // Otherwise, proceed with reallocation
    newptr = malloc(size);

    // If malloc fails, the original block is left untouched
    if (newptr == NULL)
        return NULL;

    // Copy the old data
    copysize = get_payload_size(block); // gets size of old payload
    if (size < copysize)
        copysize = size;
    memcpy(newptr, ptr, copysize);

    // Free the old block
    free(ptr);

    return newptr;
}

/**
 * @brief Allocate a block of memory with all bytes initialized as 0
 *
 * @see mm_malloc
 *
 * Returns NULL if failed
 *
 * @param[in] elements Number of elements
 * @param[in] size Size of a single element in bytes
 * @return Allocated memory payload
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
