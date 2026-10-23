#include "iod_mock.h"
#include "gtest/gtest.h"

#include <MachineClass.h>
#include <MachineInstance.h>
#include <Parameter.h>
#include <VelocityInstance.h>

#include "library_globals.cpp"

namespace {

const char *SOURCE_NAME = "velocity_source";
const char *VELOCITY_NAME = "velocity_instance";
const char *LISTENER_NAME = "velocity_listener";
const uint64_t START_US = 1000000;
const uint64_t PERIOD_US = 20000;

class VelocityInstanceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        system.activate();

        source_class = makeClass("VELOCITY_SOURCE");
        source = MachineInstanceFactory::create(SOURCE_NAME, source_class->name.c_str());
        source->setStateMachine(source_class);
        source->enable();
        ::machines[SOURCE_NAME] = source;

        velocity_class = makeVelocityClass();
        MachineClass::machine_classes["VELOCITY"] = velocity_class;
        velocity = dynamic_cast<VelocityInstance *>(
            MachineInstanceFactory::create(VELOCITY_NAME, "VELOCITY"));
        ASSERT_NE(nullptr, velocity);
        velocity->addParameter(Value(SOURCE_NAME, Value::t_symbol), source);

        listener_class = makeClass("VELOCITY_LISTENER");
        listener = MachineInstanceFactory::create(LISTENER_NAME, listener_class->name.c_str());
        listener->setStateMachine(listener_class);
        listener->enable();
        velocity->addDependancy(listener);
    }

    void TearDown() override {
        ::machines.erase(VELOCITY_NAME);
        ::machines.erase(LISTENER_NAME);
        ::machines.erase(SOURCE_NAME);
        delete listener;
        delete listener_class;
        delete velocity;
        MachineClass::machine_classes.erase("VELOCITY");
        delete velocity_class;
        delete source;
        delete source_class;
        system.deactivate();
    }

    void feedSamples(int count) {
        for (int i = 0; i < count; ++i) {
            const uint64_t elapsed_us = static_cast<uint64_t>(i) * PERIOD_US;
            source->setValue("IOTIME", Value(static_cast<int64_t>(START_US + elapsed_us)));
            source->setValue("VALUE", Value(static_cast<int64_t>(i * 10)));
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
        machine_class->parameters.push_back(Parameter("source"));
        machine_class->setProperty("VALUE", Value(0.0));
        machine_class->setProperty("Velocity", Value(0.0));
        machine_class->setProperty("IOTIME", Value(0));
        machine_class->setOption("WindowMs", 200);
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

    MockSystemSetup system;
    MachineClass *source_class = nullptr;
    MachineInstance *source = nullptr;
    MachineClass *velocity_class = nullptr;
    VelocityInstance *velocity = nullptr;
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
    EXPECT_EQ(static_cast<int64_t>(START_US + 11 * PERIOD_US), iotime.iValue);
}

TEST_F(VelocityInstanceTest, internal_updates_do_not_wake_the_instance_or_dependants) {
    velocity->enable();
    velocity->resetNeedsCheck();
    listener->resetNeedsCheck();

    feedSamples(12);

    int64_t valid = 0;
    ASSERT_TRUE(velocity->getValue("Valid").asInteger(valid));
    EXPECT_EQ(1, valid);
    EXPECT_FALSE(velocity->needsCheck());
    EXPECT_FALSE(listener->needsCheck());
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
