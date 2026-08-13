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
