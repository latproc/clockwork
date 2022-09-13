#include "gtest/gtest.h"
#include <Message.h>

#include "library_globals.c"
#if 0
#include "Statistic.h"
#include "Statistics.h"
#include <list>
    bool program_done = false;
    bool machine_is_ready = false;

    Statistics *statistics = NULL;
    std::list<Statistic *> Statistic::stats;
#endif

#include <Dispatcher.h>
#include <Logger.h>
#include <MessagingInterface.h>
#include <zmq.hpp>

#include <functional>

namespace {

    // To test messages we need a receiver and a transmitter.
    class Taker : public Receiver {
    public:
        Taker(CStringHolder name_str,
              const std::function<bool(const Message &, Transmitter *)> & can_receive)
              : Receiver(name_str), m_can_receive(can_receive) {}
        bool receives(const Message &msg, Transmitter *t) override {
            return m_can_receive(msg, t);
        }
        void handle(const Message &msg, Transmitter *from, bool needs_receipt = false) override {
            received_message = true;
        }
        void reset() { received_message = false; }
        bool got_message() { return received_message; }
    private:
        const std::function<bool(const Message &, Transmitter *)> & m_can_receive;
        bool received_message = false;
    };

    class Giver : public Transmitter {
    public:
        Giver(CStringHolder name_str) : Transmitter(name_str) {}
        virtual bool receives(const Message &msg, Transmitter *t) { return true; }
        virtual void sendMessageToReceiver(const Message &msg, Receiver *to, bool expect_reply = false) {
            if (to->receives(msg, this)) {
                to->handle(msg, this, expect_reply);
            }
        }
    };

    class TransmissionTest : public ::testing::Test {
    protected:
        void SetUp() override {}
    };

}

// The following aren't particularly useful tests, just practice TDD.
TEST(MessageTest, default_message_type_is_simple) {
    Message m;
    EXPECT_EQ(Message::SIMPLEMSG, m.getType());
}

TEST(MessageTest, default_message_text_is_empty) {
    Message m;
    EXPECT_EQ("", m.getText());
}

TEST(MessageTest, default_messages_are_equal) {
    Message m1;
    Message m2;
    EXPECT_EQ(m1, m2);
}

TEST(MessageTest, default_messages_are_not_unequal) {
    Message m1;
    Message m2;
    EXPECT_FALSE(m1 != m2);
}

TEST(MessageTest, default_messages_are_not_less_than) {
    Message m1;
    Message m2;
    EXPECT_FALSE(m1 < m2);
}

TEST(MessageTest, default_messages_are_not_greater_than) {
    Message m1;
    Message m2;
    EXPECT_FALSE(m1 > m2);
}

TEST(MessageTest, messages_with_same_text_are_equal) {
    Message m1("test");
    Message m2("test");
    EXPECT_EQ(m1, m2);
}

TEST(MessageTest, messages_with_different_text_are_unequal) {
    Message m1("test");
    Message m2("test2");
    EXPECT_NE(m1, m2);
}

TEST_F(TransmissionTest, can_send_to_receiver) {
    Message msg("test");
    Taker taker("taker", [](const Message &msg, Transmitter *t) {
        return true;
    });
    Giver giver("giver");
    giver.sendMessageToReceiver(msg, &taker);
    EXPECT_TRUE(taker.got_message());
}

TEST_F(TransmissionTest, receiver_can_refuse_message) {
    Message msg("test");
    Taker taker("taker", [](const Message &msg, Transmitter *t) {
        return false;
    });
    Giver giver("giver");
    giver.sendMessageToReceiver(msg, &taker);
    EXPECT_FALSE(taker.got_message());
}

int main(int argc, char *argv[]) {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    Dispatcher::instance();
    zmq::socket_t dispatch_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    dispatch_sync.connect("inproc://dispatcher_sync");

    ::testing::InitGoogleTest(&argc, argv);
    auto result = RUN_ALL_TESTS();

    MessagingInterface::abort();
    Dispatcher::instance()->stop();
    LogState::cleanup();
    Logger::cleanup();
    return result;
}
