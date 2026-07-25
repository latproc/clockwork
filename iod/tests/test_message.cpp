#include "gtest/gtest.h"
#include <Message.h>
#include <ThreadSafeQueue.h>
#include <Message.h>

#include "library_globals.cpp"
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
#include <boost/thread.hpp>

#include <functional>

namespace {

// To test messages we need a receiver and a transmitter.
class Taker : public Receiver {
  public:
    Taker(CStringHolder name_str,
          const std::function<bool(const Message &, Transmitter *)> &can_receive)
        : Receiver(name_str), m_can_receive(can_receive) {}
    bool receives(const Message &msg, Transmitter *t) override { return m_can_receive(msg, t); }
    void handle(const Message &msg, Transmitter *from, bool needs_receipt) override {
        received_message = true;
    }
    void reset() { received_message = false; }
    bool got_message() const { return received_message; }

  private:
    const std::function<bool(const Message &, Transmitter *)> m_can_receive;
    bool received_message = false;
};

class Giver : public Transmitter {
  public:
    Giver(CStringHolder name_str) : Transmitter(name_str) {}
    virtual bool receives(const Message &msg, Transmitter *t) { return true; }
    void sendMessageToReceiver(const Message &msg, Receiver *to,
                                       bool expect_reply) override {
        if (to->receives(msg, this)) {
            to->handle(msg, this, expect_reply);
        }
    }
};

class TransmissionTest : public ::testing::Test {
  protected:
    void SetUp() override {}
};


} // namespace

// --- CStringHolder tests ---
TEST(CStringHolderTest, CopySelfAssignmentNoCrashAndPreservesValue) {
    CStringHolder h1(strdup("alpha")); // owning
    ASSERT_STREQ(h1.get(), "alpha");

    // Self copy-assign should be a no-op and not free/dup the same pointer
    h1 = h1;
    ASSERT_STREQ(h1.get(), "alpha");
    ASSERT_TRUE(h1.will_free());
}

TEST(CStringHolderTest, MoveSelfAssignmentNoCrash) {
    CStringHolder h1(strdup("beta"));
    ASSERT_STREQ(h1.get(), "beta");

    // Self move-assign should be a no-op
    h1 = std::move(h1);
    ASSERT_STREQ(h1.get(), "beta");
    ASSERT_TRUE(h1.will_free());
}

TEST(CStringHolderTest, CopyAssignmentReplacesValueSafely) {
    CStringHolder h1(strdup("first"));
    CStringHolder h2(strdup("second"));

    h1 = h2; // h1 should now equal "second"; old "first" must have been freed
    ASSERT_STREQ(h1.get(), "second");
    ASSERT_STREQ(h2.get(), "second");
    ASSERT_TRUE(h1.will_free());
    ASSERT_TRUE(h2.will_free());
}

TEST(CStringHolderTest, MoveAssignmentTransfersOwnership) {
    CStringHolder src(strdup("payload"));
    CStringHolder dst(strdup("old"));

    dst = std::move(src);

    ASSERT_STREQ(dst.get(), "payload");
    ASSERT_TRUE(dst.will_free());
    // After move, src relinquishes ownership; get() should be null
    ASSERT_EQ(src.get(), nullptr);
}

TEST(MessageTest, CopyAssignmentSelfSafe) {
    Message m1("hello");
    m1 = m1; // self-assign must be safe
    EXPECT_EQ(m1.getText(), std::string("hello"));
}

TEST(MessageTest, CopyAssignmentReplacesFields) {
    Message a("one");
    Message b("two",Message::ENTERMSG);

    a = b;
    EXPECT_EQ(a.getText(), std::string("two"));
    EXPECT_EQ(a.getType(), b.getType());
}

TEST(MessageTest, MoveAssignmentTransfersFields) {
    Message src("move_me");
    Message dst("dest");

    dst = std::move(src);
    EXPECT_EQ(dst.getText(), std::string("move_me"));
}

TEST(MessageTest, CopyAssignmentReplacesParamsWithoutLeak) {
    auto *params_a = Message::makeParams(Value(1), Value(2));
    auto *params_b = Message::makeParams(Value(3), Value("four", Value::t_string));
    Message a("cmd_a", Message::SIMPLEMSG, params_a);
    Message b("cmd_b", Message::SIMPLEMSG, params_b);

    a = b;
    ASSERT_NE(a.getParams(), nullptr);
    ASSERT_NE(b.getParams(), nullptr);
    EXPECT_NE(a.getParams(), b.getParams());
    EXPECT_EQ(a.getText(), std::string("cmd_b"));
    EXPECT_EQ(a.getParams()->size(), 2u);
    EXPECT_EQ(a.getParams()->front(), Value(3));
    EXPECT_EQ(a.getParams()->back().asString(), std::string("four"));

    a = a;
    ASSERT_NE(a.getParams(), nullptr);
    EXPECT_EQ(a.getParams()->size(), 2u);
    EXPECT_EQ(a.getParams()->front(), Value(3));
}

TEST(MessageTest, MoveAssignmentTransfersParams) {
    auto *params = Message::makeParams(Value(9), Value(8));
    Message src("src", Message::SIMPLEMSG, params);
    Message dst("dst", Message::SIMPLEMSG, Message::makeParams(Value(1)));

    dst = std::move(src);
    ASSERT_NE(dst.getParams(), nullptr);
    EXPECT_EQ(dst.getParams()->size(), 2u);
    EXPECT_EQ(dst.getParams()->front(), Value(9));
    EXPECT_EQ(src.getParams(), nullptr);
}

TEST(MessageTest, PackageSelfAssignmentIsSafe) {
    Message msg("ping");
    Package package(nullptr, nullptr, msg);
    package = package;
    ASSERT_NE(package.message, nullptr);
    EXPECT_EQ(package.message->getText(), std::string("ping"));
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

TEST(MessageTest, copyAssignmentReplacesExistingParameters) {
    Message source("source", Message::SIMPLEMSG, Message::makeParams(Value(1)));
    Message destination("destination", Message::SIMPLEMSG, Message::makeParams(Value(2)));

    destination = source;

    ASSERT_NE(destination.getParams(), nullptr);
    EXPECT_EQ(destination.getParams()->size(), 1);
    EXPECT_EQ(destination.getParams()->front(), Value(1));
}

TEST(MessageTest, moveAssignmentReplacesExistingParameters) {
    Message source("source", Message::SIMPLEMSG, Message::makeParams(Value(1)));
    Message destination("destination", Message::SIMPLEMSG, Message::makeParams(Value(2)));

    destination = std::move(source);

    EXPECT_EQ(source.getParams(), nullptr);
    ASSERT_NE(destination.getParams(), nullptr);
    EXPECT_EQ(destination.getParams()->size(), 1);
    EXPECT_EQ(destination.getParams()->front(), Value(1));
}

TEST_F(TransmissionTest, can_send_to_receiver) {
    Message msg("test");
    Taker taker("taker", [](const Message &msg, Transmitter *t) { return true; });
    Giver giver("giver");
    giver.sendMessageToReceiver(msg, &taker, false);
    EXPECT_TRUE(taker.got_message());
}

TEST_F(TransmissionTest, receiver_can_refuse_message) {
    Message msg("test");
    Taker taker("taker", [](const Message &msg, Transmitter *t) { return false; });
    Giver giver("giver");
    giver.sendMessageToReceiver(msg, &taker, false);
    EXPECT_FALSE(taker.got_message());
}

int main(int argc, char *argv[]) {
    auto *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    boost::condition_variable_any cond_var;
    boost::shared_mutex cond_var_mutex;
    SharedThreadSafeQueue<Package*> queue(cond_var, cond_var_mutex);
    Dispatcher::create(queue);
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
