#pragma once

#include <ClientInterface.h>
#include <ControlSystemMachine.h>
#include <Dispatcher.h>
#include <Logger.h>
#include <MessageLog.h>
#include <MessagingInterface.h>
#include <ProcessingThread.h>
#include <clockwork.h>
#include <list>
class MockSystemSetup {
  public:
    class MockHardwareActivation : public HardwareActivation {
      public:
        bool initialiseHardware() override { return initialise_machines(); }
        void operator()(void) override { initialiseHardware(); }
    };
    MockSystemSetup() {
        // TODO: lots of setup needed here...
        zmq::context_t *context = new zmq::context_t;
        MessagingInterface::setContext(context);
        Logger::instance();
        Dispatcher::instance();
        MessageLog::setMaxMemory(10000);
    }
    void activate() {
        Dispatcher::instance()->reset();
        Dispatcher::start();
        ControlSystemMachine csm;
        IODCommandThread *ict = IODCommandThread::instance();
        auto &thread{ProcessingThread::create(&csm, iod_activation, *ict)};
        pt = &thread;
        iod_activation();
    }
    void deactivate() {
        Dispatcher::instance()->stop();
        pt->stop();
    }

  private:
    MockHardwareActivation iod_activation;
    ProcessingThread *pt = nullptr;
};
