// Copyright (C) 2026 Latproc contributors
//
// Regression tests for signed input decoding when a COUNTER crosses zero.

#include "IOComponent.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

TEST(IOAddressSignedInput, DecodesSigned32BitValues) {
    IOAddress signed_address(0, 0, 0, 0, 32, true);

    EXPECT_EQ(-55296, signed_address.decodeInputValue(0xffff2800u));
    EXPECT_EQ(-1, signed_address.decodeInputValue(0xffffffffu));
    EXPECT_EQ(std::numeric_limits<int32_t>::min(),
              signed_address.decodeInputValue(0x80000000u));
    EXPECT_EQ(0x12345678, signed_address.decodeInputValue(0x12345678u));
}

TEST(IOAddressSignedInput, LeavesUnsigned32BitValuesUnchanged) {
    IOAddress unsigned_address(0, 0, 0, 0, 32, false);

    EXPECT_EQ(4294912000LL, unsigned_address.decodeInputValue(0xffff2800u));
    EXPECT_EQ(4294967295LL, unsigned_address.decodeInputValue(0xffffffffu));
}

TEST(IOAddressSignedInput, DecodesSigned16BitValues) {
    IOAddress signed_address(0, 0, 0, 0, 16, true);

    EXPECT_EQ(-256, signed_address.decodeInputValue(0xff00u));
    EXPECT_EQ(-1, signed_address.decodeInputValue(0xffffu));
    EXPECT_EQ(0x1234, signed_address.decodeInputValue(0x1234u));
}

TEST(IOAddressSignedInput, LeavesUnsigned16BitValuesUnchanged) {
    IOAddress unsigned_address(0, 0, 0, 0, 16, false);

    EXPECT_EQ(65280, unsigned_address.decodeInputValue(0xff00u));
    EXPECT_EQ(65535, unsigned_address.decodeInputValue(0xffffu));
}
