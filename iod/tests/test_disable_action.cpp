#include "iod_mock.h"
#include "gtest/gtest.h"
#include <DisableAction.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <Statistic.h>
#include <Statistics.h>
#include <ThreadSafeQueue.h>
#include <Message.h>

#include "library_globals.cpp"

namespace {
class DisableActionTest : public ::testing::Test {
    void SetUp() override {
        MachineClass *fc = new MachineClass("FLAG");
        flag = MachineInstanceFactory::create("flag", "FLAG");
        flag->setStateMachine(fc);
        system.activate();
    }
    void TearDown() override { system.deactivate(); }

  private:
    MockSystemSetup system;

  protected:
    MachineInstance *flag;
};

TEST_F(DisableActionTest, DisablesTheMachineIfItExists) {

    DisableActionTemplate dat("flag");
    Action *da = dat.factory(flag);
    ASSERT_NE(da, nullptr);
    ASSERT_TRUE(flag->enabled()) << "The machine is initially enabled";
    Action::Status status = (*da)();
    ASSERT_EQ(Action::Status::Complete, status) << "The command completes";
    ASSERT_FALSE(flag->enabled()) << "The command disabled the machine";
    delete da;
}

TEST_F(DisableActionTest, FailsIfTheMachineDoesNotExist) {

    DisableActionTemplate dat("not-a-machine");
    Action *da = dat.factory(flag);
    ASSERT_NE(da, nullptr);
    ASSERT_TRUE(flag->enabled()) << "The machine is initially enabled";
    ;
    Action::Status status = (*da)();
    ASSERT_EQ(Action::Status::Failed, status) << "The command completes with a Failed status";
    ;
    delete da;
}

} // namespace
#if 0

int main(int argc, char **argv) {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    SharedThreadSafeQueue<Package*> queue;
    Dispatcher::create(queue);
    Logger::instance();
    zmq::socket_t dispatch_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    dispatch_sync.connect("inproc://dispatcher_sync");

    ::testing::InitGoogleTest(&argc, argv);
    auto result = RUN_ALL_TESTS();

    MessagingInterface::abort();
    Dispatcher::instance()->stop();
    LogState::cleanup();
    Logger::cleanup();
}
#endif
