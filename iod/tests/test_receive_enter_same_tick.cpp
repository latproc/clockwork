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
#include "ProcessingThread.h"
#include "SetStateAction.h"

#include <list>
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

} // namespace
