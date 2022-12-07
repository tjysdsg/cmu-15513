#include "util.h"
#include "csapp.h"
#include <memory.h>

void *malloc_with_data(void *src, size_t size) {
    void *ret = Malloc(size);
    memset(ret, 0, size);
    memcpy(ret, src, size);
    return ret;
}
