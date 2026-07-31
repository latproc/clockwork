
#include "gtest/gtest.h"
#include <unistd.h>

#include "clock.cpp"
#include "IODCalcAdjustClock.h"

TEST(ClockTest, CanGetClock) {
    uint64_t t1 = Clock::clock();
    usleep(2);
    uint64_t t2 = Clock::clock();
    EXPECT_TRUE(t2 > t1);
}

TEST(IODCalcAdjustClockTest, DispatchesOnceOnEachMonotonicBoundary) {
    IODCalcAdjustClock clock;

    EXPECT_FALSE(clock.due(25'000, 50, true));   // arm in the 0-49 ms slot
    EXPECT_FALSE(clock.due(49'999, 50, true));
    EXPECT_TRUE(clock.due(50'000, 50, true));
    EXPECT_FALSE(clock.due(50'999, 50, true));
    EXPECT_TRUE(clock.due(100'000, 50, true));
}

TEST(IODCalcAdjustClockTest, DisabledGroupsWaitForTheNextBoundary) {
    IODCalcAdjustClock clock;

    EXPECT_FALSE(clock.due(1'000, 100, false));
    EXPECT_FALSE(clock.due(99'999, 100, true));
    EXPECT_TRUE(clock.due(100'000, 100, true));
    EXPECT_FALSE(clock.due(150'000, 100, false));
    EXPECT_FALSE(clock.due(199'999, 100, true));
    EXPECT_TRUE(clock.due(200'000, 100, true));
}
