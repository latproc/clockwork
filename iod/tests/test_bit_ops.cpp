#include "bit_ops.h"
#include "library_globals.cpp"
#include "gtest/gtest.h"
#include <stdint.h>

// internal to IOComponent but not static..
//void set_bit(uint8_t *q, unsigned int bitpos, unsigned int val);
//void copyMaskedBits(uint8_t *dest, uint8_t*src, uint8_t *mask, size_t len);

TEST(SetBitTest, SetsCorrectBit) {
    uint8_t buf = 0;
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

TEST(GetChangesTest, ReturnsZeroWhenThereAreNoChanges) {
    uint8_t src[] = {255, 255, 0, 255};
    uint8_t dst[] = {0, 0, 255, 0};
    uint8_t mask[] = {1, 2, 4, 9};
    uint8_t last[] = {255, 255, 0, 255};
    auto changes = copyMaskedBitsAndReturnMaskOfChanges(dst, src, mask, last, 4);
    EXPECT_EQ(0, changes.count);
    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(0, changes.changes[i]);
    }
    uint8_t expected[] = {1, 2, 255 - 4, 9};
    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(dst[i], expected[i]);
    }
}

TEST(SetMaskedBitTest, DoesNotTouchUnmaskedBits) {
    EXPECT_EQ(copy_masked_bits(0x23, 0xff, 0b00000000), 0x23);
    EXPECT_EQ(copy_masked_bits(0x23, 0xff, 0b00000100), 0x27);
    EXPECT_EQ(copy_masked_bits(0b11111011, 0x00, 0b00000011), 0b11111000);
}

TEST(GetChangesTest, ReturnsCorrectMaskedData) {
    std::vector<uint8_t> src{255, 255, 0, 255};
    std::vector<uint8_t> dst{0, 0, 255, 0};
    std::vector<uint8_t> mask{1, 2, 4, 9};
    std::vector<uint8_t> last{255, 255, 0, 255};
    auto changes = copyMaskedBitsAndReturnMaskOfChanges(dst.data(), src.data(), mask.data(), last.data(), 4);
    std::vector<uint8_t> expected{1, 2, 255 - 4, 9};
    EXPECT_EQ(0, changes.count);
    EXPECT_TRUE(expected ==  dst);
}

TEST(GetChangesTest, ReturnedValueIsIndependentOfPreviousValue) {
    std::vector<uint8_t> src{255, 255, 0, 255};
    std::vector<uint8_t> dst{0, 0, 255, 0};
    std::vector<uint8_t> mask{1, 2, 4, 9};
    std::vector<uint8_t> last{255, 255, 0, 255};
    std::vector<uint8_t> expected{1, 2, 255 - 4, 9};
    auto changes = copyMaskedBitsAndReturnMaskOfChanges(dst.data(), src.data(), mask.data(), last.data(), 4);
    EXPECT_EQ(0, changes.count);
    EXPECT_TRUE(expected ==  dst);
    last[0] = 0;
    changes = copyMaskedBitsAndReturnMaskOfChanges(dst.data(), src.data(), mask.data(), last.data(), 4);
    EXPECT_EQ(1, changes.count);
    EXPECT_TRUE(expected ==  dst);
}

TEST(GetChangesTest, ReturnsCorrectChanges) {
    std::vector<uint8_t>  dst{0b00000000, 0b00000000, 0b11111111, 0b00000000};
    std::vector<uint8_t>  src{0b11111111, 0b11111110, 0b00000000, 0b11111111};
    std::vector<uint8_t> mask{0b00000001, 0b00000011, 0b00000110, 0b00001001};
    std::vector<uint8_t> last{0b11111110, 0b11111101, 0b00000000, 0b11111000};
    auto changes = copyMaskedBitsAndReturnMaskOfChanges(dst.data(), src.data(), mask.data(), last.data(), 4);
    std::vector<uint8_t> expected{1, 2, 255 - 6, 9};
    std::vector<uint8_t> expected_changes{1, 3, 0, 1};
    EXPECT_EQ(4, changes.count);
    EXPECT_TRUE(expected ==  dst);
    EXPECT_TRUE(expected_changes == changes.changes);
}

TEST(GetChangesTest, InplaceVersionReturnsCorrectMask) {
    std::vector<uint8_t> src{255, 255, 0, 255};
    std::vector<uint8_t> dst{0, 0, 255, 0};
    std::vector<uint8_t> mask{1, 2, 4, 9};
    std::vector<uint8_t> last{255, 255, 0, 255};
    std::vector<uint8_t> change_mask{1,2,3,4};
    auto changes = copyMaskedBitsAndSetChangeMask(dst.data(), change_mask.data(),
                    src.data(), mask.data(), last.data(), 4);
    std::vector<uint8_t> expected{1, 2, 255 - 4, 9};
    EXPECT_EQ(0, changes);
    EXPECT_TRUE(expected ==  dst);
}

TEST(GetChangesTest, InplaceVersionReturnsCorrectChanges) {
    std::vector<uint8_t>  dst{0b00000000, 0b00000000, 0b11111111, 0b00000000};
    std::vector<uint8_t>  src{0b11111111, 0b11111110, 0b00000000, 0b11111111};
    std::vector<uint8_t> mask{0b00000001, 0b00000011, 0b00000110, 0b00001001};
    std::vector<uint8_t> last{0b11111110, 0b11111101, 0b00000000, 0b11111000};
    std::vector<uint8_t> change_mask{1,2,3,4}; // initial data should be discarded
    auto changes = copyMaskedBitsAndSetChangeMask(dst.data(), change_mask.data(),
                    src.data(), mask.data(), last.data(), 4);
    std::vector<uint8_t> expected{1, 2, 255 - 6, 9};
    std::vector<uint8_t> expected_changes{1, 3, 0, 1};
    EXPECT_EQ(4, changes);
    EXPECT_TRUE(expected ==  dst);
    if (expected_changes != change_mask) {
        std::cout << buffer_to_string(expected_changes)
                 << " " << buffer_to_string(change_mask) << "\n";
    }
    EXPECT_TRUE(expected_changes == change_mask);
}

TEST(BitOpsIsSet, CalculatesCorrectValue) {
    std::vector<uint8_t> input(8, 0);
    for (int i=0; i<8; ++i) {
        input[i] = 1<<i;
    }

    for (int i = 0; i<64; ++i) {
        int bitpos = i%8;
        uint8_t *offset = input.data() + i/8;
        //int64_t value = (*offset & (1 << bitpos)) ? 1 : 0;
        int64_t value = is_set(offset, bitpos) ? 1 : 0;
        EXPECT_EQ(*offset, (1<<i/8));
        if (bitpos == i/8) { EXPECT_EQ(value,1); }
        else { EXPECT_EQ(value, 0); }
    }
}
