/*
    Tests for `WHEN <machine> ENTERED <state>`.

    This is a timing feature, so these tests do not evaluate an expression in
    isolation: they model the two halves of the real machinery.

      * the source's side is `MachineInstance::checkStableStates()`, the real WHEN
        pass per machine, followed by the `idle()` that executes the
        SetStateAction the matched rule enqueues. A state change is visible only
        after both, exactly as on a plant;
      * the listener's side is the rule's own `Predicate::evaluate()`, which is
        what `Condition::operator()` calls from setStableState(). One call is one
        WHEN pass for that rule.

    `setStableState()` is not called directly because it reaches for threading
    singletons a unit test has no reason to start; checkStableStates() is the
    caller processAll uses and runs the same walk. The parser side (that `WHEN x
    ENTERED S` is accepted and `WAS` is not) is covered by `cw -t` on LPC
    fixtures, per docs/WHEN_ENTERED_TEST_PLAN.md.

    The rules under test, in one place:
      1. an enter is visible to a listener whose WHEN pass has not yet run (that
         is the whole point: mqtt-fix catches the short handshake, elc analog
         absorb often does not);
      2. it is visible for exactly ONE WHEN pass;
      3. `IS` is unchanged — live only, never a queued enter;
      4. reading the edge consumes it, so a first-WHEN-wins list cannot fire a
         second rule on the same edge.
*/

#include "gtest/gtest.h"

#include <ControlSystemMachine.h>
#include <Dispatcher.h>
#include <Expression.h>
#include <Logger.h>
#include <MachineClass.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <MessagingInterface.h>
#include <ProcessingThread.h>
#include <Plugin.h>
#include <StableState.h>

#include <cstring>
#include <string>
#include <vector>

#include "library_globals.cpp"

bool prep(Stack &stack, Predicate *p, MachineInstance *m, bool left, bool reevaluate);

namespace {

const char *kSourceName = "M_TipControl";
const char *kListenerName = "M_GrabFeeder";

MachineClass *makeClass(const char *name, const std::vector<const char *> &states) {
    auto *mc = new MachineClass(name);
    for (const char *s : states) {
        mc->addState(s);
    }
    mc->initial_state = State(states.front());
    mc->default_state = State(states.front());
    return mc;
}

MachineInstance *makeMachine(const char *name, MachineClass *mc) {
    MachineInstance *mi = MachineInstanceFactory::create(name, mc->name.c_str());
    mi->setStateMachine(mc);
    mi->setInitialState();
    // The parser registers each instance in the global table; lookup() resolves
    // machine names through it, and prep() uses lookup() to find the source of a
    // `WHEN <machine> ENTERED <state>` rule. Machines are also active so their
    // stable-state walk can run.
    ::machines[name] = mi;
    mi->markActive();
    mi->enable();
    return mi;
}

// Attach `WHEN predicate` -> `state_name` to a class, as the LPC parser would.
void addRule(MachineClass *mc, const char *state_name, Predicate *predicate) {
    mc->addState(state_name);
    StableState ss(state_name, predicate);
    // The parser registers a stable state in both places; isStableState() consults
    // the xref, so a rule added to stable_states alone is not seen as a stable
    // state by setState() on this branch.
    mc->stable_state_xref.insert(std::make_pair(std::string(state_name), ss));
    mc->stable_states.push_back(ss);
}

// `WHEN <source> ENTERED <state>`.
Predicate *enteredPredicate(const char *source_name, const char *state_name) {
    return new Predicate(new Predicate(source_name), opENTERED, new Predicate(state_name));
}

// The same test written with IS, as the controls.
Predicate *isPredicate(const char *source_name, const char *state_name) {
    return new Predicate(new Predicate(source_name), opEQ, new Predicate(state_name));
}

// `property == value`, e.g. want == "on".
Predicate *propertyIs(const char *property, const char *value) {
    return new Predicate(new Predicate(property), opEQ,
                         new Predicate(Value(value, Value::t_string)));
}

std::string messageLog() {
    char *messages = MessageLog::instance()->toString(MessageLog::instance()->count());
    std::string result(messages ? messages : "");
    free(messages);
    return result;
}

// The rule engine's state walk (idle -> setState) reaches for the Dispatcher and
// ProcessingThread singletons, so a unit test that drives a real state change
// needs them to exist. It does not need their threads: nothing here polls.
class EnteredTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // The WHEN pass stamps next_poll from the global polling delay; the plant
        // sets it from SYSTEM.POLLING_DELAY (2000 us) at startup. A unit test has
        // no SYSTEM machine, so seed it here.
        if (!MachineInstance::polling_delay) {
            MachineInstance::polling_delay = new Value(2000);
        }
        MessagingInterface::setContext(new zmq::context_t);
        Logger::instance();
        MessageLog::setMaxMemory(10000);
        Dispatcher::create(dispatch_queue_);
        control_system_ = new ControlSystemMachine;
        processing_thread_ = &ProcessingThread::create(
            control_system_, hardware_activation_, *IODCommandThread::instance(), dispatch_queue_,
            mqtt_queue_);

        // The source moves between Idle and Done under the control of its own
        // `want` property, through its own WHEN rules. That gives each test
        // exact control over when the source enters and leaves a state, and the
        // transition itself happens through the real stable-state -> setState
        // path.
        // The source is a two-rule machine driven by `want`, mirroring the 2G-115
        // handshake: a process sets want=on, the machine enters Done, and the
        // next pass (the process having cleared want) leaves Done again. One
        // "tick" is runPass() enough times for that enter-and-leave to complete.
        src_class = makeClass("ENTERED_SRC", {"Idle", "Done"});
        addRule(src_class, "Done", propertyIs("want", "on"));
        addRule(src_class, "Idle", propertyIs("want", "off"));

        listener_class = makeClass("ENTERED_LISTENER", {"Wait", "TipWait", "SawIt"});
        // The listener needs a rule that can move IT into SawIt, so the test that
        // a machine never records an enter against itself has something to drive.
        addRule(listener_class, "SawIt", propertyIs("want", "on"));
        src = makeMachine(kSourceName, src_class);
        listener = makeMachine(kListenerName, listener_class);
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

    // The source's WHEN pass with `want=on`, so it enters Done. This records the
    // Done edge on the listener and leaves the source in Done (it has not been
    // asked to leave). The listener's pass is not run here.
    void sourceEntersDone() {
        src->setValue("want", Value("on", Value::t_string));
        runPass(src);
    }

    // The rest of the 2G-115 tick, stated explicitly because it is the case the
    // feature exists for: the source is back in Idle by the time the listener
    // runs, while the listener still holds an unread Done edge. In the field the
    // leave is ENTER Done doing `SET State TO Idle` -- not an enter of Idle -- so
    // nothing overwrites the edge. Here the source is settled into Idle first (a
    // real pass, which records an Idle edge of its own) and then the Done edge is
    // restored on the listener, so the assertion is about the ENTERED read and not
    // about which state happened to be recorded last.
    void sourceHasLeftDoneWithTheEdgeStillPending() {
        src->setValue("want", Value("off", Value::t_string));
        ASSERT_NE(0, changeState(src, "Idle"));
        runPass(src);
        ASSERT_EQ("Idle", currentState(src));
        listener->clearJustEntered();
        listener->noteEntered(src, std::string("Done"));
    }

    // The common case: the source enters Done and has left it again by the time
    // this returns, with one unread Done edge on the listener.
    void sourceEnters(const char *state) {
        if (strcmp(state, "Done") == 0) {
            sourceEntersDone();
            sourceHasLeftDoneWithTheEdgeStillPending();
        }
    }

    // Put the source into Done and leave it there: the `want=on` request is not
    // cleared, so the machine settles in Done. This is the level case (`IS`).
    void wantDone() {
        src->setValue("want", Value("on", Value::t_string));
        runPass(src);
        runPass(src);
    }

    // Drive the listener itself into SawIt (used by the test that shows a machine
    // never sees its own enter).
    void listenerEnters() {
        listener->setValue("want", Value("on", Value::t_string));
        runPass(listener);
    }

    // The real pass a plant runs from processAll, in its two halves:
    // checkStableStates() walks the machine's stable states in order, and the
    // matched rule enqueues a SetStateAction, which idle() then executes. A state
    // change is therefore visible only after both.
    void runPass(MachineInstance *m) {
        // checkStableStates() runs the WHEN walk; the rule that matches enqueues a
        // SetStateAction, which idle() executes. On this branch the queued action
        // is not always executed by the very first idle(), so idle() a bounded
        // number of times. It is deliberately NOT "until nothing changes": the
        // rule that just fired is still satisfied while the machine remains in the
        // state it left, so a second walk would ask for the same transition again.
        std::set<MachineInstance *> to_process;
        to_process.insert(m);
        MachineInstance::checkStableStates(to_process, 150000);
        const std::string before = m->getCurrent().getName();
        for (int i = 0; i < 8; ++i) {
            m->idle();
            if (m->getCurrent().getName() != before) {
                break;
            }
        }
    }

    // Run the machine's pass until `state` is its current state, or give up. Used
    // only where a test needs to establish a level (the source is in S), not by
    // the edge tests, which are about the edge existing at all.
    bool driveTo(MachineInstance *m, const char *state) {
        for (int i = 0; i < 8; ++i) {
            if (m->getCurrent().getName() == state) {
                return true;
            }
            runPass(m);
        }
        return m->getCurrent().getName() == state;
    }

    // One WHEN pass of one rule on the listener: the same call
    // setStableState() makes for each stable state, in order.
    bool whenPass(Predicate *rule) {
        Value result = rule->evaluate(listener);
        bool value = false;
        return result.asBoolean(value) && value;
    }

    const std::string &currentState(MachineInstance *m) { return m->getCurrent().getName(); }

  private:
    class NoopHardwareActivation : public HardwareActivation {
      public:
        bool initialiseHardware() override { return true; }
    };

    boost::condition_variable_any dispatch_cv_;
    boost::shared_mutex dispatch_mutex_;
    SharedThreadSafeQueue<Package *> dispatch_queue_{dispatch_cv_, dispatch_mutex_};
    boost::condition_variable_any mqtt_cv_;
    boost::shared_mutex mqtt_mutex_;
    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage *> mqtt_queue_{mqtt_cv_, mqtt_mutex_};
    NoopHardwareActivation hardware_activation_;
    ControlSystemMachine *control_system_ = nullptr;
    ProcessingThread *processing_thread_ = nullptr;

  protected:
    MachineInstance *src = nullptr;
    MachineInstance *listener = nullptr;
    MachineClass *src_class = nullptr;
    MachineClass *listener_class = nullptr;
};

} // namespace

// ---------------------------------------------------------------------------
// 1. The edge survives until the listener's WHEN pass, then dies
// ---------------------------------------------------------------------------

TEST_F(EnteredTest, edgeIsVisibleToAListenerWhosePassHasNotRunYet) {
    Predicate *rule = enteredPredicate(kSourceName, "Done");
    EXPECT_FALSE(whenPass(rule)) << "no enter has happened yet";
    sourceEnters("Done");
    EXPECT_EQ("Idle", currentState(src)) << "the source entered Done and left it again";
    EXPECT_TRUE(whenPass(rule)) << "the listener sees the enter on its next pass";
}

TEST_F(EnteredTest, edgeDoesNotSurviveItsOneWhenPass) {
    Predicate *rule = enteredPredicate(kSourceName, "Done");
    sourceEnters("Done");
    EXPECT_TRUE(whenPass(rule)) << "the first pass consumes the edge";
    EXPECT_FALSE(whenPass(rule)) << "a second pass must not see it: that would be a stale WAS";
}

TEST_F(EnteredTest, edgeIsVisibleEvenWhenTheSourceAlreadyLeftTheState) {
    // The 2G-115 shape: ENTER Done SETs State away in the same tick, so the source
    // is no longer in Done when the listener runs. sourceEnters("Done") leaves it
    // in Idle by construction. IS misses this (asserted by the control below);
    // ENTERED must not.
    Predicate *rule = enteredPredicate(kSourceName, "Done");
    sourceEnters("Done");
    EXPECT_EQ("Idle", currentState(src)) << "the source has already left Done";
    EXPECT_TRUE(whenPass(rule)) << "the enter is still the edge the listener cares about";
}

TEST_F(EnteredTest, isMissesThatSameShortHandshake) {
    // The control for the test above: the recorded field failure, asserted.
    Predicate *rule = isPredicate(kSourceName, "Done");
    sourceEnters("Done");
    sourceEnters("Idle");
    EXPECT_FALSE(whenPass(rule)) << "IS is a level: it cannot see an enter that already left";
}

// ---------------------------------------------------------------------------
// 2. IS is unchanged — live only, never a queued enter
// ---------------------------------------------------------------------------

TEST_F(EnteredTest, isIsTrueWhileTheSourceIsStillInTheState) {
    Predicate *rule = isPredicate(kSourceName, "Done");
    wantDone();
    EXPECT_EQ("Done", currentState(src));
    EXPECT_TRUE(whenPass(rule)) << "IS reads the live state";
    EXPECT_TRUE(whenPass(rule)) << "and keeps reading it: IS is not consumed";
}

TEST_F(EnteredTest, isIsNotConsumedByAnEnteredRead) {
    // The two operators must not share state.
    Predicate *entered = enteredPredicate(kSourceName, "Done");
    Predicate *is = isPredicate(kSourceName, "Done");
    wantDone();
    EXPECT_TRUE(whenPass(entered)) << "the ENTERED rule fires";
    EXPECT_TRUE(whenPass(is)) << "the source is still in Done, and IS was not consumed";
}

// ---------------------------------------------------------------------------
// 3. Reading consumes: a first-WHEN-wins list cannot double-fire
// ---------------------------------------------------------------------------

TEST_F(EnteredTest, aSecondRuleDoesNotSeeTheSameEdge) {
    Predicate *first = enteredPredicate(kSourceName, "Done");
    Predicate *second = enteredPredicate(kSourceName, "Done");
    sourceEnters("Done");
    EXPECT_TRUE(whenPass(first));
    EXPECT_FALSE(whenPass(second)) << "the edge belongs to one rule, not to two";
}

TEST_F(EnteredTest, twoEntersBeforeThePassStillFireOnce) {
    Predicate *rule = enteredPredicate(kSourceName, "Done");
    sourceEnters("Done");
    sourceEnters("Idle");
    sourceEnters("Done");
    EXPECT_TRUE(whenPass(rule)) << "last enter wins, no count";
    EXPECT_FALSE(whenPass(rule)) << "and exactly once, not twice";
}

TEST_F(EnteredTest, anEnterOfADifferentStateDoesNotMatch) {
    Predicate *rule = enteredPredicate(kSourceName, "Done");
    sourceEnters("Idle");
    EXPECT_FALSE(whenPass(rule)) << "the source never entered Done";
    EXPECT_FALSE(whenPass(rule)) << "and must not become true later";
}

TEST_F(EnteredTest, theEdgeIsFromTheNamedSourceNotAnySource) {
    Predicate *rule = enteredPredicate(kSourceName, "Done");
    MachineClass *other_class = makeClass("ENTERED_OTHER", {"Idle", "Done"});
    addRule(other_class, "Done", propertyIs("want", "on"));
    MachineInstance *other = makeMachine("M_Other", other_class);
    other->addDependancy(listener);

    other->setValue("want", Value("on", Value::t_string));
    EXPECT_TRUE(driveTo(other, "Done")) << "M_Other really did enter Done";
    EXPECT_FALSE(whenPass(rule)) << "the rule names M_TipControl, not M_Other";

    sourceEnters("Done");
    EXPECT_TRUE(whenPass(rule)) << "the named source still works";

    delete other;
    delete other_class;
}

// ---------------------------------------------------------------------------
// 4. Slot ownership and prep() diagnostics
// ---------------------------------------------------------------------------

TEST_F(EnteredTest, listenerDoesNotSeeItsOwnEnter) {
    // `SELF ENTERED` is rejected in prep(); the underlying slot is never filled
    // for self either, so the rule can never fire.
    listener->setValue("want", Value("on", Value::t_string));
    Predicate *rule = enteredPredicate(kListenerName, "SawIt");
    ASSERT_TRUE(driveTo(listener, "SawIt")) << "the listener itself entered SawIt";
    EXPECT_FALSE(whenPass(rule)) << "a machine never records an enter against itself";
}

TEST_F(EnteredTest, selfEnteredIsRejectedWithAMessage) {
    Predicate *rule = enteredPredicate(kListenerName, "SawIt");
    MessageLog::instance()->purge();
    EXPECT_FALSE(whenPass(rule));
    EXPECT_NE(std::string::npos, messageLog().find("SELF ENTERED"))
        << "the miss must be diagnosable, not silent";
}

TEST_F(EnteredTest, unknownStateIsReportedAndNeverFires) {
    Predicate *rule = enteredPredicate(kSourceName, "NoSuchState");
    MessageLog::instance()->purge();
    sourceEnters("Done");
    EXPECT_FALSE(whenPass(rule));
    EXPECT_NE(std::string::npos, messageLog().find("does not have"))
        << "a state name the source does not have must be reported";
}

TEST_F(EnteredTest, unknownMachineIsReportedAndNeverFires) {
    Predicate *rule = enteredPredicate("M_NoSuchMachine", "Done");
    MessageLog::instance()->purge();
    EXPECT_FALSE(whenPass(rule));
    EXPECT_NE(std::string::npos, messageLog().find("not a machine in scope"));
}

// ---------------------------------------------------------------------------
// 5. The operator node the parser produces
// ---------------------------------------------------------------------------

TEST_F(EnteredTest, nestedEnteredIsRejectedNotSilentlyMisread) {
    // `WHEN (x ENTERED S) && y IS z` cannot be answered from an edge slot read by
    // the source, and prep() must say so rather than letting resolve() bind the
    // source to its live state and quietly answer the wrong question.
    Predicate *inner = enteredPredicate(kSourceName, "Done");
    Predicate *outer = new Predicate(inner, opAND, new Predicate(Value(true)));
    MessageLog::instance()->purge();
    EXPECT_FALSE(whenPass(outer));
    EXPECT_NE(std::string::npos, messageLog().find("cannot be nested"))
        << "a nested ENTERED must be diagnosed";
}

TEST_F(EnteredTest, printsAsEnteredNotAsEquality) {
    Predicate *rule = enteredPredicate(kSourceName, "Done");
    std::stringstream ss;
    ss << *rule;
    EXPECT_NE(std::string::npos, ss.str().find("ENTERED"))
        << "shown as ENTERED so a DESCRIBE/trace does not read like IS";
    delete rule;
}
