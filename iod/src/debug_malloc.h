#ifndef __DEBUG_MALLOC_H__
#define __DEBUG_MALLOC_H__

/* Define DEBUG_MALLOC_VERBOSE before including this file for malloc/free debug messages */

#if __cplusplus
#include <cstddef>
extern "C" {
#else
#include "stddef.h"
#endif

int debug_mallocs_remaining(void);
void reset_debug_malloc(void);

char *debug_malloc(size_t size, const char *message);
char *debug_strdup(const char *str, const char *message);
void debug_free(void *block, const char *message);

/* use the following to register mallocs and frees within library code */
void did_alloc(const char *message);
void did_free(const char *message);

#if __cplusplus
}
#endif

#endif
