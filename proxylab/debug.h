#ifndef DEBUG_H
#define DEBUG_H

// #define DEBUG

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) sio_eprintf(__VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

#endif // DEBUG_H
