#include "gtest/gtest.h"

#include "ControlSystemMachine.h"
#include "Dispatcher.h"
#include "Expression.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "MessagingInterface.h"
#include "ProcessingThread.h"
#include "Scheduler.h"
#include "StableState.h"
#include "ThreadSafeQueue.h"

#include <boost/thread.hpp>
#include <chrono>
#include <set>
#include <thread>
#include <vector>

#include "library_globals.cpp"

namespace {

class WakeDuringEvaluation : public DynamicValue {
  public:
    const Value &operator()(MachineInstance *scope) override {
        scope->setNeedsCheck();
        last_result = true;
        return last_result;
    }
    const Value &operator()() override { return last_result; }
    DynamicValue *clone() const override { return new WakeDuringEvaluation; }
};

class NoopHardwareActivation : public HardwareActivation {
  public:
    bool initialiseHardware() override { return true; }
};

class TimerWakeTest : public ::testing::Test {
  protected:
    void SetUp() override {
        MessagingInterface::setContext(new zmq::context_t);
        Dispatcher::create(dispatch_queue_);

        control_system_ = new ControlSystemMachine;
        processing_thread_ = &ProcessingThread::create(
            control_system_, hardware_activation_, *IODCommandThread::instance(), dispatch_queue_,
            mqtt_queue_);

        previous_polling_delay_ = MachineInstance::polling_delay;
        MachineInstance::polling_delay = &polling_delay_;
    }

    void TearDown() override {
        if (machine_) {
            ProcessingThread::suspend(machine_);
        }
        delete machine_;
        delete machine_class_;
        Scheduler::shutdown();
        MachineInstance::polling_delay = previous_polling_delay_;
        ProcessingThread::setProcessingThreadInstance(nullptr);
        delete processing_thread_;
        delete control_system_;
        delete Dispatcher::instance();
        MessagingInterface::setContext(nullptr);
    }

    void createHoldingMachine(Predicate *condition) {
        machine_class_ = new MachineClass("TIMER_WAKE_TEST");
        machine_class_->addState("on");
        machine_class_->stable_states.push_back(StableState("on", condition));

        machine_ = MachineInstanceFactory::create("timer_wake_test", machine_class_->name);
        machine_->setStateMachine(machine_class_);
        machine_->resume(State("on"));

        ProcessingThread::suspend(machine_);
        machine_->resetNeedsCheck();
    }

    void evaluateOnce() {
        machine_->setNeedsCheck();
        ASSERT_TRUE(machine_->queuedForStableStateTest());
        ASSERT_TRUE(ProcessingThread::is_pending(machine_));

        std::set<MachineInstance *> to_process{machine_};
        ASSERT_TRUE(MachineInstance::checkStableStates(to_process, 150000));
    }

    boost::condition_variable_any dispatch_cv_;
    boost::shared_mutex dispatch_mutex_;
    SharedThreadSafeQueue<Package *> dispatch_queue_{dispatch_cv_, dispatch_mutex_};

    boost::condition_variable_any mqtt_cv_;
    boost::shared_mutex mqtt_mutex_;
    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage *> mqtt_queue_{mqtt_cv_, mqtt_mutex_};

    NoopHardwareActivation hardware_activation_;
    Value polling_delay_{2000};
    Value *previous_polling_delay_ = nullptr;
    ControlSystemMachine *control_system_ = nullptr;
    ProcessingThread *processing_thread_ = nullptr;
    MachineClass *machine_class_ = nullptr;
    MachineInstance *machine_ = nullptr;
};

TEST_F(TimerWakeTest, PreservesWakeRequestedDuringStableStateEvaluation) {
    createHoldingMachine(new Predicate(Value(new WakeDuringEvaluation)));

    evaluateOnce();

    EXPECT_TRUE(machine_->needsCheck());
    EXPECT_TRUE(machine_->queuedForStableStateTest());
    EXPECT_TRUE(ProcessingThread::is_pending(machine_));
}

TEST_F(TimerWakeTest, OverdueHoldingTimerQueuesOnlyOneFollowUpPerDeadline) {
    createHoldingMachine(new Predicate(new Predicate("TIMER"), opGE, new Predicate(1)));
    machine_->start_time = microsecs() - 2000;

    evaluateOnce();

    ASSERT_TRUE(machine_->needsCheck());
    ASSERT_TRUE(machine_->queuedForStableStateTest());
    ASSERT_TRUE(ProcessingThread::is_pending(machine_));

    // The follow-up evaluates the same absolute deadline. It must not request
    // another pass, otherwise an overdue matched hold spins forever.
    evaluateOnce();

    EXPECT_FALSE(machine_->needsCheck());
    EXPECT_FALSE(machine_->queuedForStableStateTest());
    EXPECT_FALSE(ProcessingThread::is_pending(machine_));
}

TEST_F(TimerWakeTest, NewAbsoluteDeadlineCanRecoverAgain) {
    createHoldingMachine(new Predicate(true));
    Predicate timer(new Predicate("TIMER"), opGE, new Predicate(1));

    machine_->start_time = microsecs() - 2000;
    timer.scheduleTimerEvents(nullptr, machine_, TimerOverduePolicy::RecoverOverdue);
    ASSERT_TRUE(machine_->needsCheck());

    ProcessingThread::suspend(machine_);
    machine_->resetNeedsCheck();
    machine_->start_time += 1;
    timer.scheduleTimerEvents(nullptr, machine_, TimerOverduePolicy::RecoverOverdue);

    EXPECT_TRUE(machine_->needsCheck());
    EXPECT_TRUE(machine_->queuedForStableStateTest());
    EXPECT_TRUE(ProcessingThread::is_pending(machine_));
}

TEST_F(TimerWakeTest, DInputDueRuleWakesPastSelfHold) {
    // This is the production DINPUT shape: while the input is still true the
    // machine holds its current state, while a false TIMER >= stable rule must
    // wake it when the debounce interval expires.
    machine_class_ = new MachineClass("DINPUT_TIMER_WAKE_TEST");
    machine_class_->initial_state = State("on");
    machine_class_->addState("on");
    machine_class_->addState("off");

    Predicate *self_on = new Predicate(new Predicate("SELF"), opEQ, new Predicate("on"));
    Predicate *due = new Predicate(new Predicate(new Predicate("TIMER"), opGE, new Predicate(20)),
                                   opAND, self_on);
    machine_class_->stable_states.push_back(StableState("off", due));
    machine_class_->stable_states.push_back(StableState("on", new Predicate(true)));

    machine_ = MachineInstanceFactory::create("dinput_timer_wake_test", machine_class_->name);
    machine_->setStateMachine(machine_class_);
    machine_->markActive();
    machine_->enable();

    // Remove startup timer items; this pass must be caused by the due rule.
    while (Scheduler::instance()->next()) {
        ScheduledItem *item = Scheduler::instance()->next();
        Scheduler::instance()->pop();
        delete item;
    }

    machine_->start_time = microsecs() - 19 * 1000;
    machine_->setNeedsCheck();
    std::set<MachineInstance *> to_process{machine_};
    ASSERT_TRUE(MachineInstance::checkStableStates(to_process, 150000));
    ASSERT_TRUE(Scheduler::instance()->pendingCount() > 0);

    usleep(3000);
    Scheduler::instance()->fireDueItems(microsecs());
    ASSERT_TRUE(machine_->needsCheck());

    to_process.clear();
    to_process.insert(machine_);
    ASSERT_TRUE(MachineInstance::checkStableStates(to_process, 150000));
    machine_->idle();
    EXPECT_STREQ(machine_->getCurrentStateString(), "off");
}

TEST_F(TimerWakeTest, DInputTimerStressWithTriggerReplacement) {
    constexpr size_t machine_count = 128;
    constexpr unsigned rounds = 100;

    auto *machine_class = new MachineClass("DINPUT_TIMER_STRESS");
    machine_class->initial_state = State("on");
    machine_class->addState("on");
    machine_class->addState("off");

    Predicate *self_on = new Predicate(new Predicate("SELF"), opEQ, new Predicate("on"));
    Predicate *due = new Predicate(new Predicate(new Predicate("TIMER"), opGE, new Predicate(2)),
                                   opAND, self_on);
    machine_class->stable_states.push_back(StableState("off", due));
    machine_class->stable_states.push_back(StableState("on", new Predicate(true)));

    std::vector<MachineInstance *> machines;
    machines.reserve(machine_count);
    for (size_t i = 0; i < machine_count; ++i) {
        auto name = std::string("dinput_timer_stress_") + std::to_string(i);
        MachineInstance *m = MachineInstanceFactory::create(name.c_str(), machine_class->name);
        m->setStateMachine(machine_class);
        m->markActive();
        m->enable();
        machines.push_back(m);
    }

    // Remove startup items before the stress loop.
    while (Scheduler::instance()->next()) {
        ScheduledItem *item = Scheduler::instance()->next();
        Scheduler::instance()->pop();
        delete item;
    }

    for (unsigned round = 0; round < rounds; ++round) {
        const uint64_t now = microsecs();
        std::set<MachineInstance *> to_process;
        for (MachineInstance *m : machines) {
            // Start just before the due edge so each pass arms a short timer.
            m->start_time = now - 1000;
            m->setNeedsCheck();
            to_process.insert(m);
        }
        ASSERT_TRUE(MachineInstance::checkStableStates(to_process, 150000));

        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        Scheduler::instance()->fireDueItems(microsecs());

        to_process.clear();
        for (MachineInstance *m : machines) {
            if (m->queuedForStableStateTest()) {
                to_process.insert(m);
            }
        }
        ASSERT_TRUE(MachineInstance::checkStableStates(to_process, 150000));

        // Complete queued state changes, then immediately re-arm the opposite
        // state. This repeatedly disables/replaces StableState triggers.
        for (MachineInstance *m : machines) {
            m->idle();
            m->resume(State("on"));
        }
    }

    for (MachineInstance *m : machines) {
        ProcessingThread::suspend(m);
        delete m;
    }
    delete machine_class;
    Scheduler::instance()->fireDueItems(microsecs());
    EXPECT_LT(Scheduler::instance()->pendingCount(), machine_count);
}

} // namespace
