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
        void operator()(void) override { }
    };
    MockSystemSetup() {
        // TODO: lots of setup needed here...
        zmq::context_t *context = new zmq::context_t;
        MessagingInterface::setContext(context);
        Logger::instance();
        Dispatcher::instance();
        MessageLog::setMaxMemory(10000);
        ControlSystemMachine csm;
        IODCommandThread *ict = IODCommandThread::instance();
        ProcessingThread &pt(ProcessingThread::create(&csm, iod_activation, *ict));
    }
    void activate() { iod_activation(); }

  private:
    MockHardwareActivation iod_activation;
};
