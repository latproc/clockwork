
#include "helpers.h"
#include "gtest/gtest.h"

TEST(UpdateCounterTest, ReturnsCorrectValue) {
    EXPECT_EQ(counter_adjustment(true, false), -1);
    EXPECT_EQ(counter_adjustment(false, true), 1);
    EXPECT_EQ(counter_adjustment(true, true), 0);
    EXPECT_EQ(counter_adjustment(false, false), 0);
}
