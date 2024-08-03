#include "bit_ops.h"
#include <inttypes.h>
#if VERBOSE_DEBUG
#include <iostream>
void display(uint8_t *, size_t len);
#endif

void set_bit(uint8_t *q, unsigned int bitpos, unsigned int val) {
    uint8_t bitmask = 1 << bitpos;
    if (val) {
        *q |= bitmask;
    }
    else {
        *q &= (uint8_t)(0xff - bitmask);
    }
}

void copyMaskedBits(uint8_t *dest, uint8_t *src, uint8_t *mask, size_t len) {

#if VERBOSE_DEBUG
    std::cout << "copying masked bits: \n";
    display(dest, len);
    std::cout << "\n";
    display(src, len);
    std::cout << "\n";
    display(mask, len);
    std::cout << "\n";
#endif
    size_t count = len;
    while (count--) {
        uint8_t bitmask = 0x80;
        for (int i = 0; i < 8; ++i) {
            if (*mask & bitmask) {
                if (*src & bitmask) {
                    *dest |= bitmask;
                }
                else {
                    *dest &= (uint8_t)(0xff - bitmask);
                }
            }
            bitmask = bitmask >> 1;
        }
        ++src;
        ++dest;
        ++mask;
    }
#if VERBOSE_DEBUG
    display(result, len);
    std::cout << "\n";
#endif
}

void set_mask_bits(unsigned int offset, unsigned int bitpos, unsigned int bitlen, uint8_t *result) {
    offset += bitpos / 8;
    bitpos = bitpos % 8;
    uint8_t mask = 0x01 << bitpos;
    // set  a bit in the mask for each bit of this value
    for (unsigned int i = 0; i < bitlen; ++i) {
        result[offset] |= mask;
        mask = mask << 1;
        if (!mask) {
            mask = 0x01;
            ++offset;
        }
    }
}
