#define EC_SIMULATOR
#include "iod_mock.h"
#include "gtest/gtest.h"
#include <Channel.h>
#include <SyncRemoteStatesAction.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <Statistic.h>
#include <Statistics.h>

#include "library_globals.c"

namespace {
    class SyncRemoteStatesActionTest : public ::testing::Test {
        MockSystemSetup system;
    public:
        SyncRemoteStatesActionTest() {
            MachineClass *fc = new MachineClass("FLAG");
            flag = MachineInstanceFactory::create("flag", "FLAG");
            flag->setStateMachine(fc);
            system.activate();
            channel = cd.instantiate(9999);
            assert(channel->definition() == &cd);
        }

    protected:
        virtual ~SyncRemoteStatesActionTest() {
            delete channel;
            delete flag;
        }

        MachineInstance *flag = nullptr;
        ChannelDefinition cd{"test_channel"};
        Channel *channel = nullptr;
    };

    TEST_F(SyncRemoteStatesActionTest, ConstructorDoesNotLeak) {
    auto client_channel = cd.instantiate(9998);
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
