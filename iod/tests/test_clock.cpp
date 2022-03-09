
#include "gtest/gtest.h"
#include <unistd.h>

#include "clock.cpp"

TEST(ClockTest, CanGetClock) {
    uint64_t t1 = Clock::clock();
    usleep(2);
    uint64_t t2 = Clock::clock();
    EXPECT_TRUE(t2 > t1);
}
