#ifndef __BIT_OPS_H__
#define __BIT_OPS_H__

#include <sys/types.h>
#include <stdint.h>
#if VERBOSE_DEBUG
  void display(uint8_t *, size_t len = 0);
#endif

void set_bit(uint8_t *q, unsigned int bitpos, unsigned int val);
void copyMaskedBits(uint8_t *dest, uint8_t *src, uint8_t *mask, size_t len);

#endif
