#pragma once

#include <ClientInterface.h>
#include <ControlSystemMachine.h>
#include <Dispatcher.h>
#include <Logger.h>
#include <MessagingInterface.h>
#include <ProcessingThread.h>
#include <clockwork.h>
#include <list>
    class MockSystemSetup {
        public:
            class IODHardwareActivation : public HardwareActivation {
        public:
            void operator()(void) {
                initialise_machines();
            }
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
            IODHardwareActivation iod_activation;
    };
