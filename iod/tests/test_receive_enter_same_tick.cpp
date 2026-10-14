/*
    The same-tick contract a handshake pulse relies on: a
    `RECEIVE <machine>.<state>_enter` handler runs on the same stack as the state
    change that produced the enter message, before the changing machine's
    setState() returns, with no WHEN (stable-state) pass in between.

    That is what a short pulse needs. A machine that can only see the pulse in a
    later WHEN pass misses it whenever that pass comes after the pulse has gone,
    and on the elc line the analog absorb skips the walk on analog-only ticks, so
    a Done that lasts a millisecond is never seen. The site trees catch the pulse
    with RECEIVE; this is the product-side half of that contract.

    The class-to-instance copy is part of the contract too: the parser registers a
    RECEIVE in MachineClass::receives, and setStateMachine() turns each entry into
    the instance's receives_functions. A handler added after the instance exists
    would not be seen, so it is added before it here.
*/
#include "gtest/gtest.h"

#include "ControlSystemMachine.h"
#include "Dispatcher.h"
#include "Logger.h"
#include "MachineClass.h"
#include "MachineCommandAction.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "Expression.h"
#include "ProcessingThread.h"
#include "SetStateAction.h"
#include "StableState.h"

#include <iostream>
#include <list>
#include <set>
#include <string>
#include <vector>

#include "library_globals.cpp"

namespace {

class NoopHardwareActivation : public HardwareActivation {
  public:
    bool initialiseHardware() override { return true; }
};

MachineClass *makeClass(const char *name, const std::vector<const char *> &states) {
    auto *mc = new MachineClass(name);
    for (const char *s : states) {
        mc->addState(s);
    }
    mc->initial_state = State(states.front());
    mc->default_state = State(states.front());
    return mc;
}

// `RECEIVE <message> WITHIN <state> { SET State TO <target>; }`, registered the
// way the parser does it. Call before the instance is created: setStateMachine()
// copies the class table into the instance.
void addReceiveHandler(MachineClass *mc, const char *message, const char *within,
                       const char *target_state) {
    auto *mct = new MachineCommandTemplate(message, "");
    mct->setWithinStates({within});
    std::list<ActionTemplate *> actions;
    actions.push_back(new SetStateActionTemplate(CStringHolder("SELF"), Value(target_state)));
    mct->setActionTemplates(actions);
    mc->receives.insert(std::make_pair(Message(message), mct));
}

// Attach `WHEN <predicate>` -> <state> to a class, the way the parser does: a
// stable state in both places, because isStableState() consults the xref.
void addRule(MachineClass *mc, const char *state_name, Predicate *predicate) {
    mc->addState(state_name);
    StableState ss(state_name, predicate);
    mc->stable_state_xref.insert(std::make_pair(std::string(state_name), ss));
    mc->stable_states.push_back(ss);
}

MachineInstance *makeMachine(const char *name, MachineClass *mc) {
    MachineInstance *mi = MachineInstanceFactory::create(name, mc->name.c_str());
    mi->setStateMachine(mc);
    mi->setInitialState();
    // The parser registers each instance in the global table; lookup() resolves
    // names through it. Machines are active so notifyDependents() will consider
    // them.
    ::machines[name] = mi;
    mi->markActive();
    mi->enable();
    return mi;
}

class ReceiveEnterSameTickTest : public ::testing::Test {
  protected:
    void SetUp() override {
        MessagingInterface::setContext(new zmq::context_t);
        Logger::instance();
        MessageLog::setMaxMemory(10000);
        Dispatcher::create(dispatch_queue_);
        control_system_ = new ControlSystemMachine;
        processing_thread_ = &ProcessingThread::create(
            control_system_, hardware_activation_, *IODCommandThread::instance(), dispatch_queue_,
            mqtt_queue_);

        src_class = makeClass("RECV_SRC", {"Idle", "Done"});
        listener_class = makeClass("RECV_LISTENER", {"Wait", "SawIt"});
        // Keyed on the source instance's name: this is the message a source
        // called M_RecvSrc produces on entering Done. (In a site tree the handler
        // is written on the parameter name and the loader duplicates it onto the
        // instance name.)
        addReceiveHandler(listener_class, "M_RecvSrc.Done_enter", "Wait", "SawIt");

        src = makeMachine("M_RecvSrc", src_class);
        listener = makeMachine("M_RecvListener", listener_class);
        src->addDependancy(listener);
        listener->listenTo(src);
    }

    void TearDown() override {
        delete src;
        delete listener;
        delete src_class;
        delete listener_class;
        ProcessingThread::setProcessingThreadInstance(nullptr);
        delete processing_thread_;
        delete control_system_;
        delete Dispatcher::instance();
        MessagingInterface::setContext(nullptr);
    }

    // Let a machine finish its own start-up work (enable() queues an `_enabled`
    // message to itself) so the test starts from an idle machine.
    void settle(MachineInstance *m) {
        for (int i = 0; i < 16; ++i) {
            if (!m->hasMail() && !m->executingCommand()) {
                break;
            }
            m->idle();
        }
    }

    // The state change a WHEN rule performs, run directly. executeStateChange()
    // calls setState() on the owner, which is where the enter message is
    // delivered. Nothing here runs a WHEN pass.
    void enterState(MachineInstance *m, const char *state) {
        SetStateActionTemplate ssat(CStringHolder("SELF"), Value(state));
        Action *a = ssat.factory(m);
        (*a)();
        a->release();
    }

    boost::condition_variable_any dispatch_cv_;
    boost::shared_mutex dispatch_mutex_;
    SharedThreadSafeQueue<Package *> dispatch_queue_{dispatch_cv_, dispatch_mutex_};
    boost::condition_variable_any mqtt_cv_;
    boost::shared_mutex mqtt_mutex_;
    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage *> mqtt_queue_{mqtt_cv_, mqtt_mutex_};
    NoopHardwareActivation hardware_activation_;
    ControlSystemMachine *control_system_ = nullptr;
    ProcessingThread *processing_thread_ = nullptr;

    MachineClass *src_class = nullptr;
    MachineClass *listener_class = nullptr;
    MachineInstance *src = nullptr;
    MachineInstance *listener = nullptr;
};

// The listener has moved before setState() returns, and no WHEN pass was run:
// this test never calls checkStableStates()/idle().
TEST_F(ReceiveEnterSameTickTest, ReceiveOnEnterRunsBeforeSetStateReturns) {
    ASSERT_EQ("Wait", listener->getCurrent().getName());

    enterState(src, "Done");

    EXPECT_EQ("Done", src->getCurrent().getName());
    EXPECT_EQ("SawIt", listener->getCurrent().getName())
        << "a RECEIVE on the enter message must run on the same stack as the enter";
}

// WITHIN is part of the contract: the same handler must not fire from a state
// that is not listed.
TEST_F(ReceiveEnterSameTickTest, WithinLimitsTheHandlerToTheListedState) {
    // Move the listener out of Wait first, so the enter arrives "outside" the
    // WITHIN list.
    enterState(listener, "SawIt");
    ASSERT_EQ("SawIt", listener->getCurrent().getName());

    enterState(src, "Done");

    EXPECT_EQ("Done", src->getCurrent().getName());
    EXPECT_EQ("SawIt", listener->getCurrent().getName())
        << "the handler must not fire outside its WITHIN state";
}

// The contrast that matters for the site miss: an enter with no RECEIVE handler
// moves nobody, however long the pulse lasts. This is the shape Coring.lpc had -
// a stable-state WHEN and no RECEIVE.
TEST_F(ReceiveEnterSameTickTest, AnEnterWithNoReceiveHandlerMovesNobody) {
    MachineClass *plain_class = makeClass("RECV_PLAIN_LISTENER", {"Wait", "SawIt"});
    MachineInstance *plain = makeMachine("M_RecvPlainListener", plain_class);
    src->addDependancy(plain);
    plain->listenTo(src);

    enterState(src, "Done");

    EXPECT_EQ("Wait", plain->getCurrent().getName())
        << "an enter message with no RECEIVE handler must not move the listener";
    // The registered listener still moves, so the enter itself did happen.
    EXPECT_EQ("SawIt", listener->getCurrent().getName());

    delete plain;
    delete plain_class;
}

// Internal edge work has to be visible to the scheduler AND has to finish.
//
// checkStableStates() evaluates only the machines it is handed, never their
// dependants, so a listener whose rule is `M_Src IS Done` is re-evaluated only if
// the listener itself is queued for a stable-state test. A state change must
// therefore queue its observers, and mark them change-driven so the wait-loop
// absorb can admit them promptly without admitting paced TIMER work.
//
// Queuing alone is not the whole story: if the work never completed, the mark
// would keep the absorb breaking every loop, which is itself a thrash source -
// the same failure SHOW CYCLING hunts for. So this drives the pass the work was
// queued for and requires three things: the observer runs (its rule fires), the
// mark is spent, and the machine leaves the stable-state queue.
//
// The observer here has no RECEIVE handler and no ENTERED rule: it is the Coring
// shape - a listener that can only see the pulse through an IS rule.
TEST_F(ReceiveEnterSameTickTest, InternalWorkQueuedByAChangeActuallyFinishes) {
    MachineClass *observer_class = makeClass("RECV_FINISHER", {"Wait", "SawIt"});
    // `SawIt WHEN M_RecvSrc IS Done` - the Coring shape.
    addRule(observer_class, "SawIt",
            new Predicate(new Predicate("M_RecvSrc"), opEQ, new Predicate("Done")));

    MachineInstance *observer = makeMachine("M_RecvFinisher", observer_class);
    src->addDependancy(observer);
    observer->listenTo(src);

    // Start from an idle machine: enable() queues an `_enabled` message to the
    // machine itself, and while mail is pending setNeedsCheck() routes to the
    // action path instead of the stable-state queue. (That case is already
    // admitted by has_immediate_machine_work, so it needs no mark.)
    settle(observer);
    ASSERT_FALSE(observer->hasMail()) << "precondition: no mail";
    ASSERT_FALSE(observer->executingCommand()) << "precondition: not executing";

    // Drain whatever the wiring queued, so the state change is the only input.
    {
        std::set<MachineInstance *> drain;
        drain.insert(observer);
        MachineInstance::checkStableStates(drain, 150000);
    }
    ASSERT_FALSE(observer->queuedForStableStateTest())
        << "precondition: not already queued";
    ASSERT_FALSE(observer->changeCheckPending()) << "precondition: mark clear";

    enterState(src, "Done");
    EXPECT_EQ("Done", src->getCurrent().getName()) << "the source did change state";

    // Part 1: the change makes the internal work visible to the scheduler.
    EXPECT_TRUE(observer->queuedForStableStateTest())
        << "a state change must queue its observers for a WHEN pass - the absorb "
           "guard has nothing else to key on";
    EXPECT_TRUE(observer->changeCheckPending())
        << "and it must be marked change-driven, so the absorb admits it promptly "
           "while leaving TIMER re-queues paced";

    // The pass the work was queued for, run the way the loop runs it.
    {
        std::set<MachineInstance *> pass;
        pass.insert(observer);
        MachineInstance::checkStableStates(pass, 150000);
        settle(observer);
    }

    // The work actually finished, not merely queued.
    EXPECT_EQ("SawIt", observer->getCurrent().getName())
        << "internal work queued by the change must run and take effect";
    EXPECT_FALSE(observer->changeCheckPending())
        << "the mark must be spent by the pass it was queued for";
    EXPECT_FALSE(observer->queuedForStableStateTest())
        << "and the machine must leave the stable-state queue: a mark that kept "
           "the absorb breaking every loop would itself be a thrash source";

    delete observer;
    delete observer_class;
}

} // namespace
