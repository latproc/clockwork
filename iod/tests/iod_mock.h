#pragma once

#include <ClientInterface.h>
#include <ControlSystemMachine.h>
#include <Dispatcher.h>
#include <Logger.h>
#include <MessageLog.h>
#include <MessagingInterface.h>
#include <ProcessingThread.h>
#include <clockwork.h>
#include <Message.h>
#include <ThreadSafeQueue.h>
#include <boost/thread.hpp>
#include <Scheduler.h>
#include <daemon.h>

class MockSystemSetup {
  public:
    class MockHardwareActivation : public HardwareActivation {
      public:
        bool initialiseHardware() override { return initialise_machines(); }
        void operator()(void) override { initialiseHardware(); }
    };
    MockSystemSetup() : daemon("mock-system", "mock-system", nullptr){
        //: queue(m_cond_var, m_mutex), queue2(m_cond_var2, m_mutex2), queue3(m_cond_var3, m_mutex3), queue4(m_cond_var4, m_mutex4) {
        // TODO: lots of setup needed here...
        //zmq::context_t *context = new zmq::context_t;
        //MessagingInterface::setContext(context);
        //Dispatcher::create(queue);
        //Logger::instance();
        //MessageLog::setMaxMemory(10000);
    }
    void activate() {
        Dispatcher::instance()->reset();
        Dispatcher::instance()->sync_start(); // ensure the start message is queued
        Dispatcher::start(); // start the dispatcher thread and wait for the start message
        ControlSystemMachine csm;
        IODCommandThread *ict = IODCommandThread::instance();
        auto &thread{ProcessingThread::create(csm, iod_activation, *ict, daemon, daemon.queue_manager)}; //queue, queue2, queue3, queue4)};
        pt = &thread;
        iod_activation();
    }
    void deactivate() {
        pt->stop();
        std::cout << "delete Dispatcher" << std::endl;
        Dispatcher::instance()->stop();
        delete Dispatcher::instance();
    }

  private:
    Daemon daemon;
    MockHardwareActivation iod_activation;
    // boost::condition_variable_any m_cond_var;
    // boost::shared_mutex m_mutex;
    // SharedThreadSafeQueue<Package*> queue;
    // boost::condition_variable_any m_cond_var2;
    // boost::shared_mutex m_mutex2;
    // SharedThreadSafeQueue<MachineInstance*> queue2;
    // boost::condition_variable_any m_cond_var3;
    // boost::shared_mutex m_mutex3;
    // SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> queue3;
    // boost::condition_variable_any m_cond_var4;
    // boost::shared_mutex m_mutex4;
    // SharedThreadSafeQueue<ScheduledItem*> queue4;

    ProcessingThread *pt = nullptr;
};
