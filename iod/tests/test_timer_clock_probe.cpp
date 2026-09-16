/*
    Regression guard for TIMER soft-clock progress.

    A due/hold clock rule pair is the shape real soft clocks use:

        on:  SELF IS on && TIMER < rate      (hold)
        off: SELF IS on && TIMER >= rate     (due)

    When the due item's wake is consumed, the evaluating pass finds the due
    state true and queues the transition, so the clock leaves its hold on the
    first evaluation. It must therefore reach its due state without needing the
    wake to be re-requested.

    This test documents that healthy behaviour. It passes with and without the
    re-check-retention change in checkStableStates, so it is a regression guard,
    NOT a demonstration of a defect.

    Shape explored and rejected: a rule whose holding state is also its target
    state and whose condition stays true while overdue (e.g. `held WHEN
    TIMER > 1`). There the discarded re-check is unrecoverable and the machine
    stops being evaluated. That shape is not a real clock: real clocks alternate
    on/off, so the hold condition goes false at the threshold and the due rule
    takes over. Use that degenerate shape only with the knowledge that it does
    not represent production.

    No site-tree paths or fixtures: machines are built in-process from generic
    class names.
*/

#include "gtest/gtest.h"
#include "iod_mock.h"

#include <MachineClass.h>
#include <MachineInstance.h>
#include <ProcessingThread.h>
#include <Scheduler.h>
#include <StableState.h>
#include <symboltable.h>
#include <unistd.h>
#include <value.h>

#include <set>
#include <string>

namespace {

// Two stable states forming a due/hold pair, as a real soft clock uses.
MachineInstance *makeClockMachine(const char *cls, const char *name) {
    MachineClass *mc = new MachineClass(cls);
    mc->initial_state = State("on");
    mc->addState("on");
    mc->addState("off");
    Predicate *hold = new Predicate(
        new Predicate(new Predicate(Value("SELF")), opEQ, new Predicate(Value("on"))), opAND,
        new Predicate(new Predicate(Value("TIMER")), opLT, new Predicate(Value(1))));
    Predicate *due = new Predicate(
        new Predicate(new Predicate(Value("SELF")), opEQ, new Predicate(Value("on"))), opAND,
        new Predicate(new Predicate(Value("TIMER")), opGE, new Predicate(Value(1))));
    mc->stable_states.push_back(StableState("off", due));
    mc->stable_states.push_back(StableState("on", hold));
    MachineInstance *m = MachineInstanceFactory::create(name, cls);
    m->setStateMachine(mc);
    m->markActive();
    m->enable();
    // enable() schedules a state TIMER; drain it so progress below is
    // attributable to the due rule, not a leftover item.
    while (Scheduler::instance()->next()) {
        ScheduledItem *it = Scheduler::instance()->next();
        Scheduler::instance()->pop();
        delete it;
    }
    return m;
}

MockSystemSetup *g_sys = nullptr;
void ensureSetup() {
    if (!g_sys) {
        Tokeniser::instance();
        MachineInstance::polling_delay = new Value(1000);
        g_sys = new MockSystemSetup;
    }
}

} // namespace

// The clock must reach its due state. Evaluation honours the loop's actual
// selection rule: a machine is processed only while it is queued for a
// stable-state test (the loop builds its work set from
// queuedForStableStateTest()).
TEST(ClockProgress, DueHoldPairReachesDueState) {
    ensureSetup();
    MachineInstance *m = makeClockMachine("CLOCKPROBE", "clock_probe");
    ASSERT_NE(m, nullptr);

    m->resetNeedsCheck();
    m->start_time = microsecs() - 5000 * 1000; // TIMER ~5000 vs threshold 1: overdue
    m->setNeedsCheck();
    ASSERT_TRUE(m->queuedForStableStateTest());

    bool reached_due_state = false;
    int evaluations = 0;
    for (int pass = 0; pass < 10 && !reached_due_state; ++pass) {
        if (m->queuedForStableStateTest()) {
            ++evaluations;
            std::set<MachineInstance *> to_process;
            to_process.insert(m);
            MachineInstance::checkStableStates(to_process, 150000);
            m->idle();
        }
        reached_due_state = std::string(m->getCurrentStateString()) == "off";
    }

    SCOPED_TRACE("due/hold clock never reached its due state");
    EXPECT_TRUE(reached_due_state);
    EXPECT_GT(evaluations, 0);
}
