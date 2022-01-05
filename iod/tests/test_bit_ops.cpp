#include "bit_ops.h"
#include "library_globals.c"
#include "gtest/gtest.h"

// internal to IOComponent but not static..
//void set_bit(uint8_t *q, unsigned int bitpos, unsigned int val);
//void copyMaskedBits(uint8_t *dest, uint8_t*src, uint8_t *mask, size_t len);

TEST(SetBitTest, SetsCorrectBit) {
    uint8_t buf;
    set_bit(&buf, 0, 1);
    EXPECT_EQ(buf, 1);
    set_bit(&buf, 7, 1);
    EXPECT_EQ(buf, 129);
    set_bit(&buf, 3, 1);
    EXPECT_EQ(buf, 129 + 8);
}

TEST(SetBitTest, ClearsCorrectBit) {
    uint8_t buf = 0xff;
    set_bit(&buf, 0, 0);
    EXPECT_EQ(buf, 255 - 1);
    set_bit(&buf, 7, 0);
    EXPECT_EQ(buf, 255 - 1 - 128);
    set_bit(&buf, 3, 0);
    EXPECT_EQ(buf, 255 - 1 - 128 - 8);
}

TEST(CopyMaskedBitsTest, CopiesMaskedBits) {
    uint8_t src[] = {255, 255, 0, 255};
    uint8_t dst[] = {0, 0, 255, 0};
    uint8_t mask[] = {1, 2, 4, 9};
    copyMaskedBits(dst, src, mask, 4);
    EXPECT_EQ(1, dst[0]);
    EXPECT_EQ(2, dst[1]);
    EXPECT_EQ(255 - 4, dst[2]);
    EXPECT_EQ(9, dst[3]);
}
