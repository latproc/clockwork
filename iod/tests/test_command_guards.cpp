#include "Expression.h"
#include "MachineClass.h"
#include "MachineCommandAction.h"
#include "Message.h"
#include "gtest/gtest.h"

#include "library_globals.cpp"

namespace {

MachineCommandTemplate *makeHandler(const char *name, const std::vector<std::string> &within,
                                    Predicate *guard = nullptr) {
    auto *mc = new MachineCommandTemplate(name, "");
    mc->setWithinStates(within);
    if (guard) {
        mc->setGuard(guard);
    }
    return mc;
}

TEST(CommandGuardsTest, WithinListMatchesAnyListedState) {
    MachineCommandTemplate mc("ping", "");
    mc.setWithinStates({"a", "b"});
    EXPECT_TRUE(mc.matchesWithin("a"));
    EXPECT_TRUE(mc.matchesWithin("b"));
    EXPECT_FALSE(mc.matchesWithin("c"));
}

TEST(CommandGuardsTest, EmptyWithinMatchesAnyState) {
    MachineCommandTemplate mc("ping", "");
    EXPECT_TRUE(mc.matchesWithin("a"));
    EXPECT_TRUE(mc.matchesWithin("c"));
}

TEST(CommandGuardsTest, DuplicateDetectionIgnoresWithinOrder) {
    MachineCommandTemplate mc("ping", "");
    mc.setWithinStates({"b", "a"});
    EXPECT_TRUE(mc.isDuplicateOf({"a", "b"}, nullptr));
    EXPECT_FALSE(mc.isDuplicateOf({"a"}, nullptr));
    EXPECT_FALSE(mc.isDuplicateOf({}, nullptr));
}

TEST(CommandGuardsTest, DuplicateDetectionComparesPredicateText) {
    MachineCommandTemplate mc("ping", "");
    Predicate *gt = new Predicate(new Predicate(Value(1)), opGT, new Predicate(Value(0)));
    mc.setGuard(gt);
    Predicate *same = new Predicate(new Predicate(Value(1)), opGT, new Predicate(Value(0)));
    Predicate *other = new Predicate(new Predicate(Value(2)), opGT, new Predicate(Value(0)));
    EXPECT_TRUE(mc.isDuplicateOf({}, same));
    EXPECT_FALSE(mc.isDuplicateOf({}, other));
    EXPECT_FALSE(mc.isDuplicateOf({}, nullptr));
    delete same;
    delete other;
}

TEST(CommandGuardsTest, DuringIsNotADuplicateOfGuardedCommand) {
    MachineCommandTemplate during("ping", "running", true);
    EXPECT_FALSE(during.isDuplicateOf({}, nullptr));
    EXPECT_TRUE(during.matchesWithin("anything"));
}

TEST(CommandGuardsTest, MachineClassOverlapFindsSameCommandVariant) {
    MachineClass cls("CommandGuardOverlap");
    auto *first = makeHandler("ping", {"a", "b"});
    cls.commands.insert(std::make_pair("ping", first));
    EXPECT_TRUE(cls.findOverlappingCommand("ping", {"b", "a"}, nullptr));
    EXPECT_FALSE(cls.findOverlappingCommand("ping", {"a"}, nullptr));
    EXPECT_FALSE(cls.findOverlappingCommand("ping", {}, nullptr));
}

TEST(CommandGuardsTest, MachineClassOverlapFindsReceiveVariant) {
    MachineClass cls("ReceiveGuardOverlap");
    auto *first = makeHandler("tick", {"idle"});
    cls.receives.insert(std::make_pair(Message("tick"), first));
    EXPECT_TRUE(cls.findOverlappingReceive(Message("tick"), {"idle"}, nullptr));
    EXPECT_FALSE(cls.findOverlappingReceive(Message("tick"), {}, nullptr));
}

} // namespace
