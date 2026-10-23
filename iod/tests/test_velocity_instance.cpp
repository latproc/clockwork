#include "gtest/gtest.h"

#include <Dispatcher.h>
#include <Logger.h>
#include <MachineClass.h>
#include <MachineInstance.h>
#include <MessagingInterface.h>
#include <Parameter.h>
#include <ThreadSafeQueue.h>
#include <VelocityInstance.h>
#include <IOComponent.h>
#include <zmq.hpp>

#include "library_globals.cpp"

namespace {

const char *VELOCITY_NAME = "velocity_instance";
const char *LISTENER_NAME = "velocity_listener";
const char *MODULE_NAME = "EL5152_test";
const uint64_t START_US = 1000000;
const uint64_t PERIOD_US = 20000;

class TestVelocityInstance : public VelocityInstance {
  public:
    explicit TestVelocityInstance(const char *name)
        : VelocityInstance(CStringHolder(name), "VELOCITY", MACHINE_INSTANCE) {}

    void testEnable() {
        is_enabled = true;
        start_time = microsecs();
    }
};

class VelocityInstanceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        velocity_class = makeVelocityClass();
        MachineClass::machine_classes["VELOCITY"] = velocity_class;
        velocity = new TestVelocityInstance(VELOCITY_NAME);
        velocity->setStateMachine(velocity_class);
        velocity->parameters.push_back(Parameter(Value(MODULE_NAME, Value::t_symbol)));
        velocity->parameters.push_back(Parameter(Value(static_cast<int64_t>(36))));
        input = new Input(IOAddress(1, 0, 0, 0, 32));
        velocity->io_interface = input;

        listener_class = makeClass("VELOCITY_LISTENER");
        listener = MachineInstanceFactory::create(LISTENER_NAME, listener_class->name.c_str());
        listener->setStateMachine(listener_class);
        listener->enable();
        velocity->addDependancy(listener);
    }

    void TearDown() override {
        ::machines.erase(VELOCITY_NAME);
        ::machines.erase(LISTENER_NAME);
        delete listener;
        delete listener_class;
        velocity->io_interface = nullptr;
        delete velocity;
        delete input;
        MachineClass::machine_classes.erase("VELOCITY");
        delete velocity_class;
    }

    void feedSamples(int count) {
        for (int i = 0; i < count; ++i) {
            const uint64_t elapsed_us = static_cast<uint64_t>(i) * PERIOD_US;
            input->read_time = START_US + elapsed_us;
            input->address.value = i * 10;
            VelocityInstance::sampleAll(START_US + elapsed_us);
        }
    }

    static MachineClass *makeClass(const char *name) {
        auto *machine_class = new MachineClass(name);
        machine_class->addState("ready", true);
        machine_class->initial_state = State("ready");
        machine_class->default_state = State("ready");
        return machine_class;
    }

    static MachineClass *makeVelocityClass() {
        auto *machine_class = makeClass("VELOCITY");
        machine_class->parameters.push_back(Parameter("module"));
        machine_class->parameters.push_back(Parameter("offset"));
        machine_class->setProperty("VALUE", Value(0.0));
        machine_class->setProperty("Velocity", Value(0.0));
        machine_class->setProperty("IOTIME", Value(0));
        machine_class->setOption("WindowMs", 100);
        machine_class->setOption("MinSamples", 4);
        machine_class->setOption("SampleMs", 20);
        machine_class->setOption("Scale", 1.0);
        machine_class->setOption("StaleMs", 250);
        machine_class->setOption("MaxAbsValue", 0.0);
        machine_class->setProperty("RawValue", Value(0.0));
        machine_class->setProperty("RawIOTime", Value(0));
        machine_class->setProperty("WindowStart", Value(0));
        machine_class->setProperty("WindowUs", Value(0));
        machine_class->setProperty("SampleCount", Value(0));
        machine_class->setProperty("DeltaValue", Value(0.0));
        machine_class->setProperty("DeltaTimeUs", Value(0));
        machine_class->setProperty("AgeMs", Value(0));
        machine_class->setProperty("Valid", Value(0));
        machine_class->setProperty("Rejected", Value(0));
        machine_class->setProperty("State", Value("settling", Value::t_string));
        return machine_class;
    }

    MachineClass *velocity_class = nullptr;
    TestVelocityInstance *velocity = nullptr;
    Input *input = nullptr;
    MachineClass *listener_class = nullptr;
    MachineInstance *listener = nullptr;
};

TEST_F(VelocityInstanceTest, samples_while_the_clockwork_instance_is_disabled) {
    ASSERT_FALSE(velocity->enabled());
    feedSamples(12);

    int64_t valid = 0;
    double value = 0.0;
    double feedback_velocity = 0.0;
    const Value &iotime = velocity->getValue("IOTIME");
    ASSERT_TRUE(velocity->getValue("Valid").asInteger(valid));
    ASSERT_TRUE(velocity->getValue("VALUE").asFloat(value));
    ASSERT_TRUE(velocity->getValue("Velocity").asFloat(feedback_velocity));
    EXPECT_EQ(1, valid);
    EXPECT_NEAR(500.0, value, 1.0);
    EXPECT_NEAR(value, feedback_velocity, 0.001);
    EXPECT_EQ(100000, velocity->getValue("WindowUs").iValue);
    EXPECT_EQ(static_cast<int64_t>(START_US + 11 * PERIOD_US), iotime.iValue);
}

TEST_F(VelocityInstanceTest, internal_updates_do_not_wake_the_instance_or_dependants) {
    velocity->testEnable();
    velocity->resetNeedsCheck();
    listener->resetNeedsCheck();

    feedSamples(12);

    int64_t valid = 0;
    ASSERT_TRUE(velocity->getValue("Valid").asInteger(valid));
    EXPECT_EQ(1, valid);
    EXPECT_FALSE(velocity->needsCheck());
    EXPECT_FALSE(listener->needsCheck());
}

TEST_F(VelocityInstanceTest, requests_its_configured_input_loop_cadence) {
    EXPECT_EQ(20u, VelocityInstance::minSamplePeriodMs());
    velocity->setValue("SampleMs", static_cast<int64_t>(5));
    EXPECT_EQ(5u, VelocityInstance::minSamplePeriodMs());
}

TEST_F(VelocityInstanceTest, unchanged_source_becomes_stale_without_changing_i_o_time) {
    feedSamples(12);

    const uint64_t newest_source_time = START_US + 11 * PERIOD_US;
    VelocityInstance::sampleAll(newest_source_time + 300000);

    int64_t valid = 1;
    const Value &iotime = velocity->getValue("IOTIME");
    ASSERT_TRUE(velocity->getValue("Valid").asInteger(valid));
    EXPECT_EQ(0, valid);
    EXPECT_EQ("stale", velocity->getValue("State").asString());
    EXPECT_EQ(static_cast<int64_t>(newest_source_time), iotime.iValue);
}

} // namespace

int main(int argc, char *argv[]) {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    boost::condition_variable_any cond_var;
    boost::shared_mutex cond_var_mutex;
    SharedThreadSafeQueue<Package *> queue(cond_var, cond_var_mutex);
    Dispatcher::create(queue);
    zmq::socket_t dispatch_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    dispatch_sync.connect("inproc://dispatcher_sync");

    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();

    MessagingInterface::abort();
    Dispatcher::instance()->stop();
    Logger::cleanup();
    return result;
}
