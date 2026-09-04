#include "ElcSetupRecipePolicy.h"
#include "gtest/gtest.h"

using ElcSetupRecipe::decideReapply;
using ElcSetupRecipe::kAlInit;
using ElcSetupRecipe::kAlOp;
using ElcSetupRecipe::kAlPreop;
using ElcSetupRecipe::kAlSafeop;
using ElcSetupRecipe::kHoldStuckLimitUs;
using ElcSetupRecipe::ReapplyGate;

TEST(ElcSetupReapplyPolicy, MissingSlaveWaits) {
    EXPECT_EQ(ReapplyGate::WaitVisible,
              decideReapply(false, kAlOp, false, 0));
}

TEST(ElcSetupReapplyPolicy, AlreadyOpSkipsWithoutHold) {
    EXPECT_EQ(ReapplyGate::SkipAlreadyOp,
              decideReapply(true, kAlOp, false, 0));
}

TEST(ElcSetupReapplyPolicy, PreopAndSafeopApply) {
    EXPECT_EQ(ReapplyGate::Apply, decideReapply(true, kAlPreop, false, 0));
    EXPECT_EQ(ReapplyGate::Apply, decideReapply(true, kAlSafeop, true, 1000));
}

TEST(ElcSetupReapplyPolicy, InitWaitsForPreop) {
    EXPECT_EQ(ReapplyGate::WaitPreop, decideReapply(true, kAlInit, false, 0));
    EXPECT_EQ(ReapplyGate::WaitPreop, decideReapply(true, kAlInit, true, 1000));
}

TEST(ElcSetupReapplyPolicy, HoldOnOpWaitsUntilStuckThenSkips) {
    EXPECT_EQ(ReapplyGate::WaitPreop,
              decideReapply(true, kAlOp, true, kHoldStuckLimitUs - 1));
    EXPECT_EQ(ReapplyGate::SkipAlreadyOp,
              decideReapply(true, kAlOp, true, kHoldStuckLimitUs));
}

TEST(ElcSetupReapplyPolicy, HoldStuckOnInitReleases) {
    EXPECT_EQ(ReapplyGate::ReleaseHoldStuck,
              decideReapply(true, kAlInit, true, kHoldStuckLimitUs));
}

TEST(ElcSetupReapplyPolicy, PowerDownOpWaitsInsteadOfSkip) {
    EXPECT_EQ(ReapplyGate::WaitPreop,
              decideReapply(true, kAlOp, false, 0, kHoldStuckLimitUs, true));
    EXPECT_EQ(ReapplyGate::WaitPreop,
              decideReapply(true, kAlOp, true, kHoldStuckLimitUs - 1,
                            kHoldStuckLimitUs, true));
}

TEST(ElcSetupReapplyPolicy, PowerDownHoldStuckReleasesNotSkip) {
    EXPECT_EQ(ReapplyGate::ReleaseHoldStuck,
              decideReapply(true, kAlOp, true, kHoldStuckLimitUs,
                            kHoldStuckLimitUs, true));
}

TEST(ElcSetupReapplyPolicy, PowerDownPreopApplies) {
    EXPECT_EQ(ReapplyGate::Apply,
              decideReapply(true, kAlPreop, true, 1000, kHoldStuckLimitUs, true));
}

TEST(ElcSetupReapplyPolicy, HoldNotFromInitOrZeroIdentity) {
    using ElcSetupRecipe::canBeginSetupHold;
    EXPECT_FALSE(canBeginSetupHold(kAlInit, 0x60a, 0xed310001));
    EXPECT_FALSE(canBeginSetupHold(0, 0x60a, 0xed310001));
    EXPECT_FALSE(canBeginSetupHold(kAlOp, 0, 0));
    EXPECT_TRUE(canBeginSetupHold(kAlOp, 0x60a, 0xed310001));
    EXPECT_TRUE(canBeginSetupHold(kAlPreop, 0x60a, 0xed310001));
}
