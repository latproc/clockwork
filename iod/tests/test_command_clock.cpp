// CommandClock boundary scheduling (used by the cw runtime and the iod sample
// path to decide when to wake for the next COMMANDCLOCK tick).
#include "CommandClock.h"
#include "gtest/gtest.h"

namespace {

constexpr uint64_t ms(uint64_t v) { return v * 1000ULL; }

TEST(CommandClockSchedule, DisabledNeedsNoWake) {
    CommandClock c;
    EXPECT_EQ(c.nextDueUs(ms(100), 10, false), 0ULL);
}

TEST(CommandClockSchedule, UnarmedClockWakesImmediately) {
    CommandClock c;
    // Not seen yet: the runtime must call due() once to arm it, so wake now.
    EXPECT_EQ(c.nextDueUs(ms(100), 10, true), ms(100));
}

TEST(CommandClockSchedule, ArmedClockSchedulesTheNextBoundary) {
    CommandClock c;
    EXPECT_FALSE(c.due(ms(100), 10, true)); // arms slot 10, does not fire
    EXPECT_EQ(c.nextDueUs(ms(100), 10, true), ms(110));
    EXPECT_EQ(c.nextDueUs(ms(105), 10, true), ms(110));
}

TEST(CommandClockSchedule, NewSlotWakesImmediately) {
    CommandClock c;
    EXPECT_FALSE(c.due(ms(100), 10, true)); // slot 10
    // Slot 11 has not been dispatched: the runtime must wake now, not at 120.
    EXPECT_EQ(c.nextDueUs(ms(110), 10, true), ms(110));
    EXPECT_TRUE(c.due(ms(110), 10, true));
    EXPECT_EQ(c.nextDueUs(ms(110), 10, true), ms(120));
}

TEST(CommandClockSchedule, LateDispatchRealignsToTheBoundary) {
    CommandClock c;
    EXPECT_FALSE(c.due(ms(0), 10, true));
    // A dispatch three slots late still fires once and realigns to the true
    // boundary (40 ms), so lateness never accumulates into drift.
    EXPECT_TRUE(c.due(ms(37), 10, true));
    EXPECT_EQ(c.nextDueUs(ms(37), 10, true), ms(40));
}

TEST(CommandClockSchedule, PhaseOffsetsTheBoundary) {
    CommandClock c;
    EXPECT_FALSE(c.due(ms(100), 50, true, 20)); // phase 20 → slots at 20,70,120
    EXPECT_EQ(c.nextDueUs(ms(100), 50, true, 20), ms(120));
}

TEST(CommandClockSchedule, PeriodChangeReArms) {
    CommandClock c;
    EXPECT_FALSE(c.due(ms(100), 10, true));
    EXPECT_TRUE(c.due(ms(110), 10, true));
    // A changed period is treated as unarmed so due() can re-record the slot.
    EXPECT_EQ(c.nextDueUs(ms(111), 20, true), ms(111));
}

TEST(CommandClockSchedule, ZeroPeriodFallsBackToOneSecond) {
    CommandClock c;
    EXPECT_FALSE(c.due(ms(100), 0, true)); // period 0 → 1000 ms effective
    EXPECT_EQ(c.nextDueUs(ms(100), 0, true), ms(1000));
}

} // namespace
