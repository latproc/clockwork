
#include "gtest/gtest.h"
#include <unistd.h>

#include "clock.cpp"
#include "CommandClock.h"

TEST(ClockTest, CanGetClock) {
    uint64_t t1 = Clock::clock();
    usleep(2);
    uint64_t t2 = Clock::clock();
    EXPECT_TRUE(t2 > t1);
}

TEST(CommandClockTest, DispatchesOnceOnEachMonotonicBoundary) {
    CommandClock clock;

    EXPECT_FALSE(clock.due(25'000, 50, true));   // arm in the 0-49 ms slot
    EXPECT_FALSE(clock.due(49'999, 50, true));
    EXPECT_TRUE(clock.due(50'000, 50, true));
    EXPECT_FALSE(clock.due(50'999, 50, true));
    EXPECT_TRUE(clock.due(100'000, 50, true));
}

TEST(CommandClockTest, DisabledGroupsWaitForTheNextBoundary) {
    CommandClock clock;

    EXPECT_FALSE(clock.due(1'000, 100, false));
    EXPECT_FALSE(clock.due(99'999, 100, true));
    EXPECT_TRUE(clock.due(100'000, 100, true));
    EXPECT_FALSE(clock.due(150'000, 100, false));
    EXPECT_FALSE(clock.due(199'999, 100, true));
    EXPECT_TRUE(clock.due(200'000, 100, true));
}

TEST(CommandClockTest, WouldBeDueDoesNotAdvanceSlot) {
    CommandClock clock;

    EXPECT_FALSE(clock.wouldBeDue(25'000, 50));
    EXPECT_FALSE(clock.due(25'000, 50, true));
    EXPECT_FALSE(clock.wouldBeDue(49'999, 50));
    EXPECT_TRUE(clock.wouldBeDue(50'000, 50));
    EXPECT_TRUE(clock.wouldBeDue(50'000, 50));
    EXPECT_TRUE(clock.due(50'000, 50, true));
    EXPECT_FALSE(clock.wouldBeDue(50'000, 50));
}

TEST(CommandClockTest, PhaseShiftsTheMonotonicBoundary) {
    CommandClock clock;

    EXPECT_FALSE(clock.due(3'000, 10, true, 4));  // arm in the -4..5 ms slot
    EXPECT_FALSE(clock.due(13'999, 10, true, 4));
    EXPECT_TRUE(clock.due(14'000, 10, true, 4));
    EXPECT_FALSE(clock.due(23'999, 10, true, 4));
    EXPECT_TRUE(clock.due(24'000, 10, true, 4));
}

TEST(CommandClockTest, SamePeriodDifferentPhaseDoNotShareASlot) {
    CommandClock a;
    CommandClock b;

    EXPECT_FALSE(a.due(0, 10, true, 0));
    EXPECT_FALSE(b.due(0, 10, true, 1));

    EXPECT_TRUE(a.due(10'000, 10, true, 0));
    EXPECT_FALSE(b.due(10'000, 10, true, 1));
    EXPECT_TRUE(b.due(11'000, 10, true, 1));
    EXPECT_FALSE(a.due(11'000, 10, true, 0));
}

TEST(CommandClockTest, PhaseWrapsIntoThePeriod) {
    CommandClock clock;

    EXPECT_FALSE(clock.due(1'000, 10, true, 14)); // 14 % 10 == 4
    EXPECT_TRUE(clock.due(14'000, 10, true, 14));
}
