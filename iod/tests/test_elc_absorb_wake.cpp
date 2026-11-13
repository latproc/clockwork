#include "CommandClock.h"
#include "ElcAbsorbWake.h"
#include "gtest/gtest.h"

#include <vector>

using ElcAbsorbWake::capWaitMs;
using ElcAbsorbWake::dueKeepsFanoutSkip;
using ElcAbsorbWake::isDigitalEdgeType;
using ElcAbsorbWake::whenRunsUs;

// Step the same wait iod-elc uses during analog absorb: cap the poll to the
// next CommandClock boundary and call due() when the poll returns. No bus.
static std::vector<uint64_t> firesWhileAbsorbed(bool dispatch_on_clock, uint64_t period_ms,
                                                 uint64_t duration_us, uint64_t absorb_exit_us) {
    CommandClock clock;
    std::vector<uint64_t> fires;
    uint64_t t = 0;
    while (t < duration_us) {
        const bool absorb_exit = absorb_exit_us > 0 && t != 0 && (t % absorb_exit_us) == 0;
        const bool dispatch = dispatch_on_clock || absorb_exit;
        if (dispatch && clock.due(t, period_ms, true)) {
            fires.push_back(t);
        }
        uint64_t step_us = 1000;
        if (dispatch_on_clock) {
            const uint64_t wake = clock.nextDueUs(t, period_ms, true);
            step_us = static_cast<uint64_t>(capWaitMs(20, t, wake)) * 1000ULL;
        }
        else {
            step_us = absorb_exit_us;
        }
        if (step_us < 1000) {
            step_us = 1000;
        }
        t += step_us;
    }
    return fires;
}

static uint64_t maxGap(const std::vector<uint64_t> &fires) {
    uint64_t gap = 0;
    for (size_t i = 1; i < fires.size(); ++i) {
        const uint64_t d = fires[i] - fires[i - 1];
        if (d > gap) {
            gap = d;
        }
    }
    return gap;
}

TEST(ElcAbsorbWake, ClockDuringAbsorbStaysWithinTwoPeriods) {
    const uint64_t period_ms = 25;
    const auto fires = firesWhileAbsorbed(true, period_ms, 2000000ULL, 395000ULL);
    ASSERT_GE(fires.size(), 40u);
    EXPECT_LE(maxGap(fires), period_ms * 2ULL * 1000ULL);
}

TEST(ElcAbsorbWake, DispatchOnlyWhenAbsorbExitsIsThe395msShape) {
    const uint64_t period_ms = 25;
    const auto fires = firesWhileAbsorbed(false, period_ms, 2000000ULL, 395000ULL);
    ASSERT_GE(fires.size(), 2u);
    EXPECT_GT(maxGap(fires), period_ms * 2ULL * 1000ULL);
}

TEST(ElcAbsorbWake, PointEdgeRunsBeforeStableRecheck) {
    const uint64_t edge = 500000;
    const uint64_t stable = 4000;
    EXPECT_EQ(edge, whenRunsUs(true, edge, stable));
    EXPECT_EQ(edge + stable, whenRunsUs(false, edge, stable));
    EXPECT_LT(whenRunsUs(true, edge, stable), whenRunsUs(false, edge, stable));
}

TEST(ElcAbsorbWake, AnalogAndCounterAreNotDigitalEdges) {
    EXPECT_TRUE(isDigitalEdgeType("POINT"));
    EXPECT_TRUE(isDigitalEdgeType("STATUS_FLAG"));
    EXPECT_TRUE(isDigitalEdgeType("FLAG"));
    EXPECT_FALSE(isDigitalEdgeType("ANALOGINPUT"));
    EXPECT_FALSE(isDigitalEdgeType("COUNTER"));
    EXPECT_FALSE(isDigitalEdgeType(nullptr));
}

// K=1, one notify(false) per late due, no continue poll in between.
// Pending fan-out must be finished, not restarted at consumer 0.
TEST(ElcAbsorbWake, LateDueReachesEveryConsumer) {
    const int n = 3;
    int sent[3] = {0, 0, 0};
    size_t skip = 0;
    bool pending = false;
    auto notify = [&](bool continue_fanout) {
        bool cont = continue_fanout;
        if (!cont && dueKeepsFanoutSkip(pending)) {
            cont = true;
        }
        if (!cont) {
            skip = 0;
            pending = false;
        }
        size_t idx = 0;
        size_t nsent = 0;
        bool more = false;
        const size_t limit = cont ? static_cast<size_t>(-1) : 1;
        for (int i = 0; i < n; ++i) {
            if (idx < skip) {
                ++idx;
                continue;
            }
            if (nsent >= limit) {
                more = true;
                break;
            }
            ++sent[i];
            ++nsent;
            ++idx;
            skip = idx;
        }
        pending = more;
        if (!more) {
            skip = 0;
        }
    };
    for (int due = 0; due < 4; ++due) {
        notify(false);
    }
    EXPECT_GE(sent[0], 1);
    EXPECT_GE(sent[1], 1);
    EXPECT_GE(sent[2], 1);
}
