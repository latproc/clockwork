#include "Dispatcher.h"
#include "Expression.h"
#include "Logger.h"
#include "MachineClass.h"
#include "MachineCommandAction.h"
#include "HandleMessageAction.h"
#include "MachineInstance.h"
#include "Message.h"
#include "ThreadSafeQueue.h"
#include "gtest/gtest.h"
#include <boost/thread.hpp>
#include <cstdlib>

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

// Runtime accept = MachineCommand::matches (WITHIN list / WHEN / unrestricted).
// COMMANDCLOCK notify skips the dependant when this is false.
// Mirrors tests/unit/command_guards.cw (listed, gated, ping fallback, calcAdjust).

class CommandAcceptTest : public ::testing::Test {
  protected:
    MachineClass *cls = nullptr;
    MachineInstance *mi = nullptr;

    static void EnsureDispatcher() {
        static boost::condition_variable_any cond;
        static boost::shared_mutex mutex;
        static SharedThreadSafeQueue<Package *> queue(cond, mutex);
        static bool created = false;
        if (!created) {
            Logger::instance();
            Dispatcher::create(queue);
            created = true;
        }
    }

    void SetUp() override {
        EnsureDispatcher();
        cls = new MachineClass("CommandAcceptSubject");
        cls->addState("a");
        cls->addState("b");
        cls->addState("c");
        cls->addState("idle");
        cls->addState("active");
        cls->addState("stopping");
        cls->addState("stopped");

        cls->receives.insert(std::make_pair(Message("listed"), makeHandler("listed", {"a", "b"})));
        cls->receives.insert(std::make_pair(Message("split"), makeHandler("split", {"a"})));
        cls->receives.insert(std::make_pair(Message("split"), makeHandler("split", {"b"})));

        Predicate *when_true =
            new Predicate(new Predicate("counter"), opGT, new Predicate(0));
        cls->receives.insert(
            std::make_pair(Message("gated"), makeHandler("gated", {}, when_true)));

        cls->receives.insert(std::make_pair(Message("ping"), makeHandler("ping", {"a", "b"})));
        Predicate *ping_when =
            new Predicate(new Predicate("counter"), opGT, new Predicate(0));
        cls->receives.insert(
            std::make_pair(Message("ping"), makeHandler("ping", {"c"}, ping_when)));
        cls->receives.insert(std::make_pair(Message("ping"), makeHandler("ping", {})));

        cls->receives.insert(
            std::make_pair(Message("calcAdjust"), makeHandler("calcAdjust", {"idle"})));
        cls->receives.insert(
            std::make_pair(Message("calcAdjust"), makeHandler("calcAdjust", {"active"})));
        cls->receives.insert(
            std::make_pair(Message("calcAdjust"), makeHandler("calcAdjust", {"stopping"})));

        cls->receives.insert(
            std::make_pair(Message("only_tick"), makeHandler("only_tick", {"a"})));

        const std::string inst =
            std::string("cg_") + ::testing::UnitTest::GetInstance()->current_test_info()->name();
        mi = MachineInstanceFactory::create(inst.c_str(), "CommandAcceptSubject");
        mi->setStateMachine(cls);
        mi->setValue("counter", 0);
    }

    void TearDown() override { delete mi; }

    void go(const char *state) { mi->getCurrent() = State(state); }
};

TEST_F(CommandAcceptTest, ListedWithinAnyOfTheStates) {
    go("a");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("listed"));
    go("b");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("listed"));
    go("c");
    EXPECT_FALSE(mi->acceptsCommandInCurrentState("listed"));
}

TEST_F(CommandAcceptTest, SplitHandlersAreIndependent) {
    go("a");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("split"));
    go("b");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("split"));
    go("c");
    EXPECT_FALSE(mi->acceptsCommandInCurrentState("split"));
}

TEST_F(CommandAcceptTest, WhenPredicateGatesDispatch) {
    go("a");
    mi->setValue("counter", 0);
    EXPECT_FALSE(mi->acceptsCommandInCurrentState("gated"));
    mi->setValue("counter", 1);
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("gated"));
}

TEST_F(CommandAcceptTest, PingListWhenThenUnrestrictedFallback) {
    mi->setValue("counter", 0);
    go("a");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("ping"));
    go("b");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("ping"));
    go("c");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("ping")) << "unrestricted fallback";
    mi->setValue("counter", 1);
    go("c");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("ping")) << "WHEN handler also matches";
}

TEST_F(CommandAcceptTest, CalcAdjustSkippedWhenStopped) {
    go("stopped");
    EXPECT_FALSE(mi->acceptsCommandInCurrentState("calcAdjust"));
    go("idle");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("calcAdjust"));
    go("active");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("calcAdjust"));
    go("stopping");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("calcAdjust"));
}

TEST_F(CommandAcceptTest, ReceiveOnlyInListedState) {
    go("c");
    EXPECT_FALSE(mi->acceptsCommandInCurrentState("only_tick"));
    go("a");
    EXPECT_TRUE(mi->acceptsCommandInCurrentState("only_tick"));
}

TEST_F(CommandAcceptTest, UnknownCommandIsNotAccepted) {
    go("a");
    EXPECT_FALSE(mi->acceptsCommandInCurrentState("no_such"));
    EXPECT_FALSE(mi->acceptsCommandInCurrentState(nullptr));
}

TEST_F(CommandAcceptTest, FanoutSendsOneDependantPerCall) {
    MachineClass *dep_cls = new MachineClass("FanoutDep");
    dep_cls->addState("idle");
    dep_cls->receives.insert(std::make_pair(Message("ping"), makeHandler("ping", {})));

    MachineInstance *a = MachineInstanceFactory::create("fanout_a", "FanoutDep");
    a->setStateMachine(dep_cls);
    a->enable();
    MachineInstance *b = MachineInstanceFactory::create("fanout_b", "FanoutDep");
    b->setStateMachine(dep_cls);
    b->enable();
    mi->addDependancy(a);
    mi->addDependancy(b);
    mi->enable();

    mi->notifyCommandConsumers("ping", 100, false);
    EXPECT_TRUE(mi->hasCommandFanoutPending()) << "K=1 leaves the second dependant";

    mi->notifyCommandConsumers("ping", 100, true);
    EXPECT_FALSE(mi->hasCommandFanoutPending()) << "second poll drains the remainder";

    delete b;
    delete a;
}

TEST_F(CommandAcceptTest, ThinReceiveQueuesOnActionStackNotMail) {
    unsetenv("IOD_THIN_RECEIVE");
    EXPECT_FALSE(MachineInstance::thinReceiveEnabled());

    MachineClass *dep_cls = new MachineClass("ThinRecvDep");
    dep_cls->addState("idle");
    dep_cls->receives.insert(std::make_pair(Message("ping"), makeHandler("ping", {})));
    MachineInstance *dep = MachineInstanceFactory::create("thin_recv_dep", "ThinRecvDep");
    dep->setStateMachine(dep_cls);
    dep->enable();
    mi->addDependancy(dep);
    mi->enable();

    setenv("IOD_THIN_RECEIVE", "1", 1);
    ASSERT_TRUE(MachineInstance::thinReceiveEnabled());
    mi->notifyCommandConsumers("ping", 100, false);

    size_t thin_new = 0;
    for (Action *act : dep->active_actions) {
        ThinReceiveAction *thin = dynamic_cast<ThinReceiveAction *>(act);
        if (thin && thin->getStatus() == Action::New) {
            ++thin_new;
        }
    }
    EXPECT_EQ(thin_new, 1u) << "notify enqueues ThinReceive, does not run it";
    EXPECT_FALSE(dep->hasPending(Message("ping")));

    mi->notifyCommandConsumers("ping", 100, false);
    size_t thin_total = 0;
    for (Action *act : dep->active_actions) {
        if (dynamic_cast<ThinReceiveAction *>(act)) {
            ++thin_total;
        }
    }
    EXPECT_EQ(thin_total, 1u) << "second notify coalesces while New is queued";

    unsetenv("IOD_THIN_RECEIVE");
    delete dep;
}

TEST_F(CommandAcceptTest, InvokeReceiveUsesTheSameHandlerAsListed) {
    go("a");
    Message listed("listed");
    Action *handler = nullptr;
    const Action::Status st = mi->invokeReceive(listed, mi, false, &handler);
    EXPECT_TRUE(st == Action::Complete || st == Action::Running);
    if (handler) {
        handler->release();
    }
    go("c");
    handler = nullptr;
    EXPECT_EQ(mi->invokeReceive(listed, mi, false, &handler), Action::Complete);
    EXPECT_EQ(handler, nullptr);
}

TEST_F(CommandAcceptTest, RegistersOnlyCommandClockInstances) {
    const size_t before = MachineInstance::commandClockCount();
    MachineInstance *other = MachineInstanceFactory::create("clk_other", "Undefined");
    EXPECT_EQ(MachineInstance::commandClockCount(), before);
    MachineInstance *clock = MachineInstanceFactory::create("clk_index", "COMMANDCLOCK");
    EXPECT_EQ(MachineInstance::commandClockCount(), before + 1);
    delete clock;
    EXPECT_EQ(MachineInstance::commandClockCount(), before);
    delete other;
    EXPECT_EQ(MachineInstance::commandClockCount(), before);
}

} // namespace
