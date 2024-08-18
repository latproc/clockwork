#include "library_globals.cpp"
#include "gtest/gtest.h"
#include <split_string.h>
#include "bit_ops.h"

// internal to IOComponent but not static..
//void set_bit(uint8_t *q, unsigned int bitpos, unsigned int val);
//void copyMaskedBits(uint8_t *dest, uint8_t*src, uint8_t *mask, size_t len);

TEST(SplitString, ReturnsNullForNullInput) {
    char **strings = split_string(nullptr);
    EXPECT_EQ(strings, nullptr);
    EXPECT_EQ(mallocs_remaining(), 0);
}

TEST(SplitString, ReturnsArrayWithOneNullStringForEmptyString) {
    char **strings = split_string("");
    EXPECT_NE(strings, nullptr);
    EXPECT_EQ(strings[0], nullptr);
    release_params(strings);
    EXPECT_EQ(mallocs_remaining(), 0);
}

TEST(SplitString, ReturnsArrayWithOneString) {
    char **strings = split_string("test");
    EXPECT_NE(strings, nullptr);
    EXPECT_EQ(strcmp(strings[0], "test"), 0);
    EXPECT_EQ(strings[1], nullptr);
    release_params(strings);
    EXPECT_EQ(mallocs_remaining(), 0);
}
