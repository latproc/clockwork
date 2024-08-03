#ifndef __BIT_OPS_H__
#define __BIT_OPS_H__

#include <stdint.h>
#include <sys/types.h>
#if VERBOSE_DEBUG
void display(uint8_t *, size_t len = 0);
#endif

void set_bit(uint8_t *q, unsigned int bitpos, unsigned int val);
void copyMaskedBits(uint8_t *dest, uint8_t *src, uint8_t *mask, size_t len);
void set_mask_bits(unsigned int offset, unsigned int bitpos, unsigned int bitlen, uint8_t *mask);

#endif
