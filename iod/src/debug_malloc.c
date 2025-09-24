#include "debug_malloc.h"
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

static int debug_malloc_count = 0;
static int debug_free_count = 0;

void reset_debug_malloc(void) {
    debug_malloc_count = 0;
    debug_free_count = 0;
}

char *debug_malloc(size_t size, const char *message) {
#ifdef DEBUG_MALLOC_VERBOSE
    printf("alloc %s\n", message);
#endif
    ++debug_malloc_count;
    return malloc(size);
}

char *debug_strdup(const char *str, const char *message) {
#ifdef DEBUG_MALLOC_VERBOSE
    printf("alloc %s\n", message);
#endif
    ++debug_malloc_count;
    return strdup(str);
}

void debug_free(void *block, const char *message) {
#ifdef DEBUG_MALLOC_VERBOSE
    printf("free %s\n", message);
#endif
    ++debug_free_count;
    free(block);
}

void did_alloc(const char *message) {
#ifdef DEBUG_MALLOC_VERBOSE
    printf("alloc %s\n", message);
#endif
    ++debug_malloc_count;
}

void did_free(const char *message) {
#ifdef DEBUG_MALLOC_VERBOSE
    printf("free %s\n", message);
#endif
    --debug_malloc_count;
}

int debug_mallocs_remaining(void) { return debug_malloc_count - debug_free_count; }
