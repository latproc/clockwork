#ifndef __BIT_OPS_H__
#define __BIT_OPS_H__

#include <stdint.h>
#include <sys/types.h>
#include <string>
#include <vector>

std::string buffer_to_string(const uint8_t *p, size_t len);
std::string buffer_to_string(const std::vector<uint8_t> &buf);

void set_bit(uint8_t *q, unsigned int bitpos, unsigned int val);
void copyMaskedBits(uint8_t *dest, const uint8_t *src, const uint8_t *mask, size_t len);

// set  a bit in the mask for each bit of this value
void set_mask_bits(unsigned int offset, unsigned int bitpos, unsigned int bitlen, uint8_t *mask);

// returns init with some bits from src, depending on the mask
uint8_t copy_masked_bits(uint8_t init, uint8_t src, uint8_t mask);

struct ChangeSet {
    size_t count = 0;
    std::vector<uint8_t> changes;
};

size_t copyMaskedBitsAndSetChangeMask(uint8_t *dest, uint8_t *change_mask, const uint8_t *src, const uint8_t *mask, const uint8_t *last, size_t len);

ChangeSet copyMaskedBitsAndReturnMaskOfChanges(uint8_t *dest, const uint8_t *src, const uint8_t *mask, const uint8_t *last, size_t len);

#endif
