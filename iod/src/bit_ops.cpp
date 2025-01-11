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

bool is_set(uint8_t *offset, unsigned int bitpos) {
    return (*offset & (1 << bitpos)) ? true : false;
}

// applies mask to src and returns init + masked values from src
uint8_t copy_masked_bits(uint8_t init, uint8_t src, uint8_t mask) {
    uint8_t result = init;
    uint8_t bitmask = 0x80;
    while (bitmask) {
        if (mask & bitmask) {
            if (src & bitmask) {
                result |= bitmask;
            }
            else {
                result &= (uint8_t)(0xff - bitmask);
            }
        }
        bitmask = bitmask >> 1;
    }
    return result;
}

void copyMaskedBits(uint8_t *dest, const uint8_t *src, const uint8_t *mask, size_t len) {
    size_t count = len;
    while (count--) {
        if (*mask) { *dest = copy_masked_bits(*dest, *src, *mask); }
        ++src;
        ++dest;
        ++mask;
    }
}

// applies mask to src and returns init + masked values from src
uint8_t copy_masked_bits(uint8_t dest, uint8_t src, uint8_t mask,
                uint8_t last, uint8_t &change_mask, size_t &change_count) {
    uint8_t result = dest;
    uint8_t bitmask = 0x80;
    change_mask = 0x00;
    while (bitmask) {
        if (mask & bitmask) {
            if ((src & bitmask) != (last & bitmask)) {
                change_mask |= bitmask;
                ++change_count;
            }
            if (src & bitmask) {
                result |= bitmask;
            }
            else {
                result &= (uint8_t)(0xff - bitmask);
            }
        }
        bitmask = bitmask >> 1;
    }
    return result;
}
// Applies mask to src and last and compares results.
// Where there is a difference a bit is set in the change mask.
// mask applied to dest and masked bits are set to match src
size_t copyMaskedBitsAndSetChangeMask(uint8_t *dest, uint8_t *change_mask, const uint8_t *src, const uint8_t *mask, const uint8_t *last, size_t len) {
    size_t num_changes = 0;
    size_t count = len;
    while (count--) {
        if (*mask) {
            *dest = copy_masked_bits(*dest, *src, *mask, *last, *change_mask, num_changes);
#if 0
            uint8_t bitmask = 0x80;
            while (bitmask) {
                if (*mask & bitmask) {
                    if ((*src & bitmask) != (*last & bitmask)) {
                        *change_mask |= bitmask;
                        ++num_changes;
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
#endif
        }
        ++src;
        ++dest;
        ++mask;
        ++last;
        ++change_mask;
    }
    return num_changes;
}

ChangeSet copyMaskedBitsAndReturnMaskOfChanges(uint8_t *dest, const uint8_t *src, const uint8_t *mask, const uint8_t *last, size_t len) {
    ChangeSet cs;
    cs.changes.resize(len);
    uint8_t *change_ptr = cs.changes.data();
    size_t count = len;
    while (count--) {
        uint8_t bitmask = 0x80;
        if (*mask) {
            *dest = copy_masked_bits(*dest, *src, *mask, *last, *change_ptr, cs.count);
#if 0
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
#endif
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
