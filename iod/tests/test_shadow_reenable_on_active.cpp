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

    // One owner machine plus the mirror that stands in for it on the channel.
    void makeMirrorChannel() {
        iface_class_ = new MachineClass("TESTINTERFACE");

        mirror_ = MachineInstanceFactory::create("M_TestMirror", "TESTINTERFACE",
                                                 MachineInstance::MACHINE_SHADOW);
        ASSERT_NE(nullptr, mirror_);
        mirror_->setStateMachine(iface_class_);

        def_ = new ChannelDefinition("test_mirror_channel");
        def_->addUpdates("M_TestMirror", "TESTINTERFACE");

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

} // namespace
