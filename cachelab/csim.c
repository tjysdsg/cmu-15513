/**
 * @file Cache simulator. Read a trace file and output the number of cache hit,
 * misses, evictions, dirty bytes, and dirty bytes evicted.
 * @details Usage:
 *      ./csim [-v] -s <s> -b <b> -E <E> -t <trace>
 *      -h          Print this help message and exit
 *      -v          Verbose mode: report effects of each memory operation
 *      -s <s>      Number of set index bits (there are 2**s sets)
 *      -b <b>      Number of block bits (there are 2**b blocks)
 *      -E <E>      Number of lines per set (associativity)
 *      -t <trace>  File name of the memory trace to process
 *
 *      The -s, -b, -E, and -t options must be supplied for all simulations.
 *
 *  The trace file follows this format:
 *
 *  Op Addr,Size
 *
 *  Op denotes the type of memory access. It can be either L for a load, or S
 *  for a store.
 *  Addr gives the memory address to be accessed. It should be a 64-bit
 *  hexadecimal number, without a leading 0x.
 *  Size gives the number of bytes to be accessed at Addr.
 *  It should be a small, positive decimal number.
 *
 * @author Jiyang Tang <jiyangta@andrew.cmu.edu>
 */

#include "cachelab.h"
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define INVALID_OPTION_NUM (-1)
#define ADDR_BITS 64
#define TRACE_MAX_LINE_LEN 256

/// is verbose mode enabled
static bool verbose = false;

/**
 * A struct describing a memory access
 */
typedef struct {
    /// the type of memory operation (load/store)
    enum {
        MEMORY_ACCESS_LOAD,
        MEMORY_ACCESS_STORE,
    } op;
    /// memory address being operated on
    unsigned long addr;
    /// size in bytes
    unsigned long size;
} memory_access_t;

/**
 * Print to stdout if in verbose mode
 */
#define print_if_verbose(...)                                                  \
    do {                                                                       \
        if (verbose) {                                                         \
            printf(__VA_ARGS__);                                               \
        }                                                                      \
    } while (false)

/**
 * Process a memory access trace file into an array of memory accesses.
 *
 * @param trace Name of the trace file to process
 * @param ma An array of memory accesses is stored here
 * @param n Number of memory accesses is stored here
 * @return 0 if successful , 1 if there were errors
 */
int process_trace_file(const char *trace, memory_access_t **ma, int *n) {
    FILE *f = fopen(trace, "rt");
    if (!f) {
        fprintf(stderr, "Error opening '%s': %s\n", trace, strerror(errno));
        return 1;
    }

    int n_traces = 0;
    size_t max_n_traces = 256;
    memory_access_t *mem_accesses =
        calloc(max_n_traces, sizeof(memory_access_t));

    char linebuf[TRACE_MAX_LINE_LEN];
    while (fgets(linebuf, TRACE_MAX_LINE_LEN, f)) {
        if (n_traces >= (int)max_n_traces) {
            max_n_traces *= 2;
            mem_accesses =
                realloc(mem_accesses, max_n_traces * sizeof(memory_access_t));
        }

        // op
        char op = linebuf[0];
        if (op == 'L')
            mem_accesses[n_traces].op = MEMORY_ACCESS_LOAD;
        else if (op == 'S')
            mem_accesses[n_traces].op = MEMORY_ACCESS_STORE;
        else {
            fprintf(stderr, "Invalid Op %c: %s\n", op, linebuf);
            return 1;
        }

        if (linebuf[1] != ' ') {
            fprintf(stderr, "Expect a space between Op and Addr: %s\n",
                    linebuf);
            return -1;
        }

        // addr
        char *addr_end;
        unsigned long addr = strtoul(linebuf + 2, &addr_end, 16);
        if (addr_end == linebuf + 2) {
            fprintf(stderr, "Expect a memory address: %s\n", linebuf);
            return -1;
        }
        mem_accesses[n_traces].addr = addr;

        // size
        if (*addr_end == ',')
            addr_end += 1;
        else {
            fprintf(stderr, "Expect a comma between Addr and Size: %s\n",
                    linebuf);
            return 1;
        }
        char *tmp;
        unsigned long size = strtoul(addr_end, &tmp, 10);
        if (addr_end == tmp) {
            fprintf(stderr, "Invalid Size: %s\n", linebuf);
            return 1;
        }
        mem_accesses[n_traces].size = size;

        ++n_traces;
    }
    fclose(f);

    *ma = mem_accesses;
    *n = n_traces;
    return 0;
}

/**
 * A struct describing a single cache block
 */
typedef struct {
    /// valid bit
    bool valid;
    /// is this dirty
    bool dirty;
    /// tag, used for distinguishing different memory addresses with the same
    /// set index
    uint64_t tag;
    /// the time of last visit, 0 means oldest. Used for LRU
    int last_visit;
} cache_block_t;

/**
 * Simulate a trace described in ma and output statistics to stats
 * @param ma An array of memory accesses
 * @param stats Statistics output
 * @param n Number of memory accesses
 * @param s Number of set index bits
 * @param b Number of block bits
 * @param E Number of lines per set
 */
void simulate_traces(memory_access_t *ma, csim_stats_t *stats, int n, int s,
                     int b, int E) {
    memset(stats, 0, sizeof(csim_stats_t));

    int n_sets = 1 << s;
    cache_block_t *cache = calloc((size_t)(n_sets * E), sizeof(cache_block_t));

    // set index mask
    uint64_t s_mask = (unsigned)~0 >> (ADDR_BITS - s);
    if (s == 0)
        s_mask = 0;

    int timer = 1;
    for (int i = 0; i < n; ++i) {
        memory_access_t m = ma[i];
        uint64_t set_idx = (m.addr >> b) & s_mask;
        uint64_t tag = m.addr >> (s + b);

        // check all blocks in a set
        cache_block_t *set_start = cache + (set_idx * (unsigned)E);
        bool hit = false;
        cache_block_t *first_invalid_block = NULL;
        cache_block_t *oldest_block = NULL;
        for (cache_block_t *block = set_start; block < set_start + E; ++block) {
            if (block->valid) {
                if (block->tag == tag) { // hit
                    hit = true;
                    block->last_visit = timer;

                    // write-back
                    if (m.op == MEMORY_ACCESS_STORE)
                        block->dirty = true;

                    break;
                }

                // update the oldest block
                if (!oldest_block ||
                    block->last_visit < oldest_block->last_visit)
                    oldest_block = block;
            } else if (!first_invalid_block) { // miss
                first_invalid_block = block;   // prepare for possible eviction
            }
        }

        print_if_verbose("%c %lx,%ld", m.op == MEMORY_ACCESS_LOAD ? 'L' : 'S',
                         m.addr, m.size);
        if (hit) {
            ++stats->hits;
            print_if_verbose(" hit");
        } else {
            ++stats->misses;
            print_if_verbose(" miss");

            if (!first_invalid_block ||
                first_invalid_block >= set_start + E) { // eviction
                print_if_verbose(" eviction");
                ++stats->evictions;

                oldest_block->tag = tag;
                oldest_block->valid = true;
                oldest_block->last_visit = timer;

                // dirty eviction
                if (oldest_block->dirty) {
                    stats->dirty_evictions += 1u << b;
                    oldest_block->dirty = false;
                }

                // write-allocate
                if (m.op == MEMORY_ACCESS_STORE)
                    oldest_block->dirty = true;
            } else {
                first_invalid_block->tag = tag;
                first_invalid_block->valid = true;
                first_invalid_block->last_visit = timer;

                // write-allocate
                if (m.op == MEMORY_ACCESS_STORE)
                    first_invalid_block->dirty = true;
            }
        }

        print_if_verbose("\n");
        ++timer;
    }

    // count the number of dirty bytes at the end of the sim
    for (int i = 0; i < n_sets * E; ++i) {
        if (cache[i].dirty)
            stats->dirty_bytes += 1u << b;
    }

    free(cache);
}

/**
 * Print program usage
 */
void usage(void) {
    printf("Usage: ./csim [-v] -s <s> -b <b> -E <E> -t <trace>\n"
           "-h          Print this help message and exit\n"
           "-v          Verbose mode: report effects of each memory "
           "operation\n"
           "-s <s>      Number of set index bits (there are 2**s sets)\n"
           "-b <b>      Number of block bits (there are 2**b blocks)\n"
           "-E <E>      Number of lines per set (associativity)\n"
           "-t <trace>  File name of the memory trace to process\n"
           "\n"
           "The -s, -b, -E, and -t options must be supplied for all "
           "simulations.\n");
}

/**
 * Convert number option from string to int, and check if the value is valid.
 * @param opt_name Option name
 * @param val Value in string
 * @param max Max allowed value
 * @return The converted number in int if successful, otherwise -1
 */
int convert_number_option(const char *opt_name, const char *val, int max) {
    char *tmp;
    int ret = (int)strtoul(val, &tmp, 10);
    if (tmp == optarg) {
        fprintf(stderr, "Expect an integer after: -%s!\n", opt_name);
        return -1;
    }

    if (ret < 0 || ret > max) {
        fprintf(stderr, "-%s must be within [0, %d]!\n", opt_name, ret);
        return -1;
    }
    return ret;
}

int main(int argc, char **argv) {
    int s = INVALID_OPTION_NUM;
    int b = INVALID_OPTION_NUM;
    int E = INVALID_OPTION_NUM;
    char *trace_file = NULL;

    int c = 0;
    while (true) {
        c = getopt(argc, argv, "hvs:b:E:t:");
        if (c == -1)
            break;

        switch (c) {
        case 'h':
            usage();
            exit(0);
        case 'v':
            verbose = true;
            break;
        case 's':
            if (-1 == (s = convert_number_option("s", optarg, ADDR_BITS)))
                exit(1);
            break;
        case 'b':
            if (-1 == (b = convert_number_option("b", optarg, ADDR_BITS)))
                exit(1);
            break;
        case 'E':
            if (-1 == (E = convert_number_option("E", optarg, INT32_MAX)))
                exit(1);
            break;
        case 't':
            trace_file = optarg;
            break;
        case '?': // getopt will print error message
            exit(1);
        default:
            fprintf(stderr, "Invalid command line options\n");
            exit(1);
        }
    }

    // check for required options
    if (s == INVALID_OPTION_NUM || b == INVALID_OPTION_NUM ||
        E == INVALID_OPTION_NUM || !trace_file) {
        fprintf(stderr, "The -s, -b, -E, and -t options must be supplied!\n");
        usage();
        exit(1);
    }
    // check if s + b is within range
    if (s + b > ADDR_BITS) {
        fprintf(stderr, "s + b must be within [0, %d]!\n", ADDR_BITS);
        exit(1);
    }

    memory_access_t *mem_accesses = NULL;
    int n_traces = 0;
    if (process_trace_file(trace_file, &mem_accesses, &n_traces))
        exit(1);

    csim_stats_t stats;
    simulate_traces(mem_accesses, &stats, n_traces, s, b, E);

    printSummary(&stats);

    free(mem_accesses);
    return 0;
}
