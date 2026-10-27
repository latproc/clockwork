// COMMANDCLOCK boundary scheduling for the IOD sample path and transport-free cw.
#include "CommandClock.h"
#include "gtest/gtest.h"

namespace {

constexpr uint64_t ms(uint64_t value) { return value * 1000ULL; }

TEST(CommandClockSchedule, DispatchesOnceOnEachMonotonicBoundary) {
    CommandClock clock;
    EXPECT_FALSE(clock.due(ms(25), 50, true));
    EXPECT_FALSE(clock.due(ms(49) + 999, 50, true));
    EXPECT_TRUE(clock.due(ms(50), 50, true));
    EXPECT_FALSE(clock.due(ms(50) + 999, 50, true));
    EXPECT_TRUE(clock.due(ms(100), 50, true));
}

TEST(CommandClockSchedule, DisabledClockWaitsForTheNextBoundary) {
    CommandClock clock;
    EXPECT_FALSE(clock.due(ms(1), 100, false));
    EXPECT_FALSE(clock.due(ms(99) + 999, 100, true));
    EXPECT_TRUE(clock.due(ms(100), 100, true));
    EXPECT_FALSE(clock.due(ms(150), 100, false));
    EXPECT_FALSE(clock.due(ms(199) + 999, 100, true));
    EXPECT_TRUE(clock.due(ms(200), 100, true));
}

TEST(CommandClockSchedule, PhaseSeparatesSamePeriodClocks) {
    CommandClock first;
    CommandClock second;
    EXPECT_FALSE(first.due(ms(0), 10, true, 0));
    EXPECT_FALSE(second.due(ms(0), 10, true, 1));
    EXPECT_TRUE(first.due(ms(10), 10, true, 0));
    EXPECT_FALSE(second.due(ms(10), 10, true, 1));
    EXPECT_TRUE(second.due(ms(11), 10, true, 1));
}

TEST(CommandClockSchedule, BoundaryWakeAndLateDispatchDoNotDrift) {
    CommandClock clock;
    EXPECT_FALSE(clock.due(ms(100), 10, true));
    EXPECT_EQ(clock.nextDueUs(ms(100), 10, true), ms(110));
    EXPECT_TRUE(clock.due(ms(137), 10, true));
    EXPECT_EQ(clock.nextDueUs(ms(137), 10, true), ms(140));
}

TEST(CommandClockSchedule, DisabledClockNeedsNoWake) {
    CommandClock clock;
    EXPECT_EQ(clock.nextDueUs(ms(100), 10, false), 0ULL);
}

} // namespace
