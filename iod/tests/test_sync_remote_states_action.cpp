#define EC_SIMULATOR
#include "iod_mock.h"
#include "gtest/gtest.h"
#include <Channel.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <Statistic.h>
#include <Statistics.h>
#include <SyncRemoteStatesAction.h>

#include "library_globals.cpp"

namespace {
class SyncRemoteStatesActionTest : public ::testing::Test {
    MockSystemSetup *system = nullptr;

    void SetUp() override {
        std::cout << "SyncRemoteStatesActionTest::SetUp" << std::endl;
        system = new MockSystemSetup();
        MachineClass *fc = new MachineClass("FLAG");
        flag = MachineInstanceFactory::create("flag", "FLAG");
        flag->setStateMachine(fc);
        system->activate();
        cd = new ChannelDefinition{"test_channel"};
        channel = cd->instantiate(9999);
        assert("channel definition is not set" && channel->definition() == cd);
    }
    void TearDown() override {
        std::cout << "SyncRemoteStatesActionTest::TearDown" << std::endl;
        delete channel;
        delete flag;
        system->deactivate();
        //Dispatcher::instance()->stop();
        //delete Dispatcher::instance();
        delete system;
    }
  public:

  protected:

    MachineInstance *flag = nullptr;
    ChannelDefinition *cd = nullptr;
    Channel *channel = nullptr;
};

TEST_F(SyncRemoteStatesActionTest, ConstructorDoesNotLeak) {
    auto client_channel = cd->instantiate(9998);
    client_channel->setDefinitionLocation("dummy-file", 1);

    //    auto sock_ptr = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PAIR);
    //    SyncRemoteStatesActionTemplate srsat(channel, sock_ptr);
    //    Action *srsa = srsat.factory(flag);
    //    ASSERT_NE(srsa, nullptr);
    //    Action::Status status = (*srsa)();
    //    ASSERT_EQ(Action::Status::Complete, status) << "The command completes";
    //    delete srsa;
    //    delete sock_ptr;
}

} // namespace
