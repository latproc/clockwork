#ifndef __WIN32_HELPER_H__
#define __WIN32_HELPER_H__

#ifdef WIN32
#include <stdint.h>
void usleep(uint64_t);
#endif

#endif
