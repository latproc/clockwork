#include "gtest/gtest.h"
#include <Message.h>
#include <symboltable.h>
#include <MessageLog.h>
#include <memory>

#include "library_globals.c"

#if 0
TEST(CStringHolderTests, CanConstructFromConstCharPtr) {
  CStringHolder x("hello");
  EXPECT_EQ(0, strcmp(x.get(),"hello"));
}

TEST(CStringHolderTests, CanConstructFromCharPtr) {
  CStringHolder x{strdup("hello")};
  EXPECT_EQ(0, strcmp(x.get(),"hello"));
}

#include <zmq.hpp>
#include <MessagingInterface.h>
#include <Logger.h>
#include <Dispatcher.h>
#endif

void f(CStringHolder cs) {
  CStringHolder x(cs);
}

CStringHolder g(CStringHolder cs) {
  CStringHolder x(cs);
  return x;
}

int main(int argc, char *argv[]) {
//  zmq::context_t *context = new zmq::context_t;
//  MessagingInterface::setContext(context);
//  Logger::instance();
//  Dispatcher::instance();
  {
  CStringHolder x("hello");
  }
  {
  CStringHolder x{strdup("hello")};
  }
  f("test");
  {
    CStringHolder x = g("hello");
  }
#if 0
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
#endif

}

