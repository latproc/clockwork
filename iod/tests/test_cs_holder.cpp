#include "gtest/gtest.h"
#include <Message.h>
#include <MessageLog.h>
#include <memory>
#include <symboltable.h>

#include "library_globals.cpp"

#if 1
TEST(CStringHolderTests, CanConstructFromConstCharPtr)
{
    CStringHolder x("hello");
    EXPECT_EQ(0, strcmp(x.get(), "hello"));
    EXPECT_TRUE(x.will_free());
}

TEST(CStringHolderTests, CanConstructFromCharPtr)
{
    CStringHolder x{strdup("hello")};
    EXPECT_EQ(0, strcmp(x.get(), "hello"));
    EXPECT_TRUE(x.will_free());
}

TEST(CStringHolderTests, CanCopyConstruct)
{
    CStringHolder x("hello");
    CStringHolder y(x);
    EXPECT_EQ(0, strcmp(y.get(), "hello"));
    EXPECT_TRUE(y.will_free());
}

TEST(CStringHolderTests, CanAssign)
{
    CStringHolder x("hello");
    CStringHolder y;
    y = x;
    EXPECT_EQ(0, strcmp(y.get(), "hello"));
    EXPECT_TRUE(y.will_free());
}

TEST(CStringHolderTests, CanMoveConstruct)
{
    CStringHolder x("hello");
    CStringHolder y(std::move(x));
    EXPECT_EQ(0, strcmp(y.get(), "hello"));
    EXPECT_TRUE(y.will_free());
    EXPECT_EQ(nullptr, x.get());
}

#include <Dispatcher.h>
#include <Logger.h>
#include <MessagingInterface.h>
#include <zmq.hpp>
#endif

void f(CStringHolder cs) { CStringHolder x(cs); }

CStringHolder g(CStringHolder cs) {
    CStringHolder x(cs);
    assert("copy constructed holder should free if the source would" && x.will_free() == cs.will_free());
    return x;
}

int main(int argc, char *argv[]) {
    //  zmq::context_t *context = new zmq::context_t;
    //  MessagingInterface::setContext(context);
    //  Logger::instance();
    //  Dispatcher::instance();
    { CStringHolder x("hello");
      assert("holder needs to free its copy" && x.will_free());
    }
    { CStringHolder x{strdup("hello")};
      assert("holder should free" && x.will_free());
    }
    f("test");
    { CStringHolder x = g("hello");
      assert("holder needs to strdup" && x.will_free());
    }
#if 1
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
#endif
}
