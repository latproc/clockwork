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
