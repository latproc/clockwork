// Regression: entering ACTIVE must (re-)enable the mirror machines a channel
// owns.
//
// A channel disables its mirrors when it leaves ACTIVE.  The enable that runs
// on CONNECTED/WAITSTART may not happen again before the channel returns to
// ACTIVE (an overlapping dual start, or a handshake that is already past those
// states).  The mirrors then stay disabled while the channel reports ACTIVE:
// they ignore state changes and commands aimed at them fail, with nothing in
// the log to say why.  Entering ACTIVE has to enable them too.
#include "gtest/gtest.h"

#include "Channel.h"
#include "CallMethodAction.h"
#include "ControlSystemMachine.h"
#include "Dispatcher.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "ProcessingThread.h"

#include "library_globals.cpp"

namespace {

class NoopHardwareActivation : public HardwareActivation {
  public:
    bool initialiseHardware() override { return true; }
};

class ShadowReenableOnActiveTest : public ::testing::Test {
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
    }

    void TearDown() override {
        delete channel_;
        channel_ = nullptr;
        delete def_;
        def_ = nullptr;
        ProcessingThread::setProcessingThreadInstance(nullptr);
        delete processing_thread_;
        processing_thread_ = nullptr;
        delete control_system_;
        control_system_ = nullptr;
        delete Dispatcher::instance();
        MessagingInterface::setContext(nullptr);
    }

    // A mirror is enabled by an action queued on the channel, so the channel
    // has to be given a chance to run it.
    void drainChannel() {
        for (int i = 0; i < 200 && channel_->executingCommand(); ++i) {
            channel_->idle();
        }
    }

    // Names are process-global registries, so every fixture needs its own:
    // MachineInstance::find returns the first match in all_machines, and a
    // second fixture reusing a name would be handed the earlier machine.
    static std::string uniqueSuffix() {
        static int n = 0;
        return std::to_string(++n);
    }

    // One owner machine plus the mirror that stands in for it on the channel.
    void makeMirrorChannel() {
        const std::string suffix = uniqueSuffix();
        mirror_name_ = "M_TestMirror_" + suffix;
        def_name_ = "test_mirror_channel_" + suffix;

        iface_class_ = new MachineClass("TESTINTERFACE");

        mirror_ = MachineInstanceFactory::create(mirror_name_.c_str(), "TESTINTERFACE",
                                                 MachineInstance::MACHINE_SHADOW);
        ASSERT_NE(nullptr, mirror_);
        mirror_->setStateMachine(iface_class_);
        // The parser registers machines in the global table; the factory does
        // not, and a lookup by name (which is how a CALL resolves its target)
        // reads that table.
        machines[mirror_name_] = mirror_;

        def_ = new ChannelDefinition(def_name_.c_str());
        def_->addUpdates(mirror_name_.c_str(), "TESTINTERFACE");

        channel_ = Channel::create(7788, def_);
        ASSERT_NE(nullptr, channel_);
        ASSERT_EQ(def_, channel_->definition());

        // A disabled machine clears queued work instead of running it, so the
        // channel has to be live for its queued enable actions to take effect.
        channel_->enable();
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

    MachineClass *iface_class_ = nullptr;
    MachineInstance *mirror_ = nullptr;
    std::string mirror_name_;
    std::string def_name_;
    ChannelDefinition *def_ = nullptr;
    Channel *channel_ = nullptr;
};

// The mirror must end up enabled when the channel reaches ACTIVE by any route,
// not only when CONNECTED/WAITSTART happen to run on the way.
TEST_F(ShadowReenableOnActiveTest, MirrorIsEnabledWhenChannelReachesActive) {
    makeMirrorChannel();

    EXPECT_FALSE(mirror_->enabled())
        << "a mirror starts disabled and is only enabled by the channel";

    // A connection is needed before the channel will accept a state change, and
    // it is what queues the first handshake step (WAITSTART on this side).
    channel_->addConnection();
    drainChannel();
    EXPECT_TRUE(mirror_->enabled()) << "WAITSTART enables the mirrors the channel owns";

    channel_->setState(ChannelImplementation::DISCONNECTED);
    EXPECT_FALSE(mirror_->enabled()) << "leaving ACTIVE disables the mirrors it owns";

    // The regression: reach ACTIVE without CONNECTED/WAITSTART running again.
    channel_->setState(ChannelImplementation::ACTIVE);
    drainChannel();

    EXPECT_TRUE(mirror_->enabled())
        << "entering ACTIVE must re-enable a mirror left disabled by the disconnect";
}

// The mirror can also be disabled with no channel transition to follow, so
// entering ACTIVE is not enough: the periodic reconcile has to put it back.
TEST_F(ShadowReenableOnActiveTest, MirrorDisabledWhileChannelIsActiveIsReconciled) {
    makeMirrorChannel();

    channel_->addConnection();
    drainChannel();
    channel_->setState(ChannelImplementation::ACTIVE);
    drainChannel();
    ASSERT_TRUE(channel_->getCurrent() == ChannelImplementation::ACTIVE);
    ASSERT_TRUE(mirror_->enabled());

    mirror_->disable();
    ASSERT_FALSE(mirror_->enabled());

    // The reconcile is throttled to a few seconds, and the housekeeping loop is
    // driven by the processing thread, so poll rather than assume one pass.
    for (int i = 0; i < 400 && !mirror_->enabled(); ++i) {
        channel_->reconcileShadows();
        drainChannel();
        if (!mirror_->enabled()) {
            usleep(20000);
        }
    }

    EXPECT_TRUE(mirror_->enabled())
        << "a mirror disabled while the channel is ACTIVE must be reconciled";
}

// A CALL to a disabled mirror must still be refused, but the log has to say
// that it is a stale mirror on a live channel - the bare "Call to disabled
// machine" line reads as a wiring or CALL fault and was taken for one.
TEST_F(ShadowReenableOnActiveTest, CallToADisabledMirrorNamesTheMirrorAndChannel) {
    makeMirrorChannel();

    channel_->addConnection();
    drainChannel();
    channel_->setState(ChannelImplementation::ACTIVE);
    drainChannel();
    ASSERT_TRUE(channel_->getCurrent() == ChannelImplementation::ACTIVE);

    mirror_->disable();
    ASSERT_FALSE(mirror_->enabled());

    MachineClass *caller_class = new MachineClass("TESTCALLER");
    MachineInstance *caller = MachineInstanceFactory::create("M_TestCaller", "TESTCALLER");
    ASSERT_NE(nullptr, caller);
    caller->setStateMachine(caller_class);

    CallMethodActionTemplate tpl(CStringHolder("SomeCommand"),
                                 CStringHolder(mirror_name_.c_str()));
    CallMethodAction action(caller, tpl);

    EXPECT_EQ(Action::Failed, action.run()) << "a CALL to a disabled mirror is still refused";

    const std::string err = action.error() ? action.error() : "";
    EXPECT_NE(std::string::npos, err.find(mirror_name_))
        << "the refusal must name the mirror: " << err;
    EXPECT_NE(std::string::npos, err.find("stale mirror"))
        << "the refusal must say the mirror is stale, not that the machine is merely "
           "disabled: "
        << err;

    delete caller;
    delete caller_class;
}

} // namespace
