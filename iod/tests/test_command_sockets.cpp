#include "gtest/gtest.h"

#include "Channel.h"
#include "ClientInterface.h"
#include "ControlSystemMachine.h"
#include "Dispatcher.h"
#include "Logger.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "ProcessingThread.h"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "library_globals.cpp"

namespace {

class NoopHardwareActivation : public HardwareActivation {
  public:
    bool initialiseHardware() override { return true; }
};

class CommandSocketTest : public ::testing::Test {
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
        for (Channel *chn : channels_) {
            delete chn;
        }
        channels_.clear();
        for (ChannelDefinition *def : defs_) {
            delete def;
        }
        defs_.clear();
        ProcessingThread::setProcessingThreadInstance(nullptr);
        delete processing_thread_;
        delete control_system_;
        delete Dispatcher::instance();
        MessagingInterface::setContext(nullptr);
    }

    Channel *makeChannel(const char *name, unsigned int port) {
        auto *def = new ChannelDefinition(name);
        defs_.push_back(def);
        Channel *chn = Channel::create(port, def);
        channels_.push_back(chn);
        return chn;
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
    std::vector<ChannelDefinition *> defs_;
    std::vector<Channel *> channels_;
};

TEST_F(CommandSocketTest, SetupIsIdempotentAcrossThreads) {
    Channel *a = makeChannel("cmdsock_a", 7701);
    Channel *b = makeChannel("cmdsock_b", 7702);
    Channel *c = makeChannel("cmdsock_c", 7703);

    ASSERT_TRUE(a->hasCommandSocket());
    ASSERT_TRUE(b->hasCommandSocket());
    ASSERT_TRUE(c->hasCommandSocket());
    const std::string addr_a = a->commandSocketAddress();
    const std::string addr_b = b->commandSocketAddress();
    const std::string addr_c = c->commandSocketAddress();
    EXPECT_NE(addr_a, addr_b);
    EXPECT_NE(addr_a, addr_c);
    EXPECT_NE(addr_b, addr_c);

    std::atomic<int> errors{0};
    auto hammer = [&]() {
        for (int i = 0; i < 80; ++i) {
            try {
                Channel::setupCommandSockets();
                a->setDefinition(defs_[0]);
                b->setDefinition(defs_[1]);
                c->setDefinition(defs_[2]);
            }
            catch (...) {
                ++errors;
            }
        }
    };
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back(hammer);
    }
    for (auto &th : threads) {
        th.join();
    }

    EXPECT_EQ(0, errors.load());
    EXPECT_TRUE(a->hasCommandSocket());
    EXPECT_TRUE(b->hasCommandSocket());
    EXPECT_TRUE(c->hasCommandSocket());
    EXPECT_EQ(addr_a, a->commandSocketAddress());
    EXPECT_EQ(addr_b, b->commandSocketAddress());
    EXPECT_EQ(addr_c, c->commandSocketAddress());
    EXPECT_EQ(3u, processing_thread_->commandChannelCount());
    EXPECT_EQ(a->commandSocketInfo(), a->commandSocketInfo());
}

TEST_F(CommandSocketTest, PairConnectsToTheSingleBoundSocket) {
    Channel *chn = makeChannel("cmdsock_pair", 7710);
    ASSERT_TRUE(chn->hasCommandSocket());

    zmq::socket_t peer(*MessagingInterface::getContext(), ZMQ_PAIR);
    peer.connect(chn->commandSocketAddress().c_str());
    usleep(1000);

    const char *ping = "ping";
    zmq::message_t req(ping, 4);
    ASSERT_TRUE(peer.send(req));

    zmq::pollitem_t item = {(void *)(*chn->commandSocketInfo()->sock), 0, ZMQ_POLLIN, 0};
    ASSERT_GE(zmq::poll(&item, 1, 500), 0);
    ASSERT_TRUE(item.revents & ZMQ_POLLIN);

    zmq::message_t got;
    ASSERT_TRUE(chn->commandSocketInfo()->sock->recv(&got));
    ASSERT_EQ(4u, got.size());
    EXPECT_EQ(0, memcmp(got.data(), ping, 4));
}

} // namespace
