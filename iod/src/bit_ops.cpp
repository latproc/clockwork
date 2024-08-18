#include "bit_ops.h"
#include <inttypes.h>
#include <sstream>
#include <iomanip>
#include <stdint.h>

std::string buffer_to_string(const uint8_t *p, size_t len) {
    std::stringstream ss;
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i]
                  << std::dec;
    }
    return ss.str();
}

std::string buffer_to_string(const std::vector<uint8_t> &buf) {
    return buffer_to_string(buf.data(), buf.size());
}

#if VERBOSE_DEBUG
void show_buffers(uint8_t *dest, uint8_t *src, uint8_t *mask, size_t len) {
    std::cout << "copying masked bits: \n";
    std::cout << buffer_to_string(dest, len);
    std::cout << "\n";
    std::cout << buffer_to_string(src, len);
    std::cout << "\n";
    std::cout << buffer_to_string(mask, len);
    std::cout << "\n";
}
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

void copyMaskedBits(uint8_t *dest, const uint8_t *src, const uint8_t *mask, size_t len) {
    size_t count = len;
    while (count--) {
        uint8_t bitmask = 0x80;
        while (bitmask) {
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
}

ChangeSet copyMaskedBitsAndReturnMaskOfChanges(uint8_t *dest, const uint8_t *src, const uint8_t *mask, const uint8_t *last, size_t len) {
    ChangeSet cs;
    cs.changes.resize(len);
    uint8_t *change_ptr = cs.changes.data();
    size_t count = len;
    while (count--) {
        uint8_t bitmask = 0x80;
        while (bitmask) {
            if (*mask & bitmask) {
                if ((*src & bitmask) != (*last & bitmask)) {
                    *change_ptr |= bitmask;
                    ++cs.count;
                }
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
        ++last;
        ++change_ptr;
    }
    return cs;
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
