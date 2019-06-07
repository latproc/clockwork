#include <DisableAction.h>
#include "gtest/gtest.h"
#include <Statistic.h>
#include <Statistics.h>
#include <Dispatcher.h>
#include <MessageLog.h>
#include <MachineInstance.h>
#include <MessagingInterface.h>
#include <Logger.h>
#include <clockwork.h>
#include <list>

#include "library_globals.c"

namespace {
	class DisableActionTest : public ::testing::Test {
		void SetUp() override {
			// TODO: lots of setup needed here...
			zmq::context_t *context = new zmq::context_t;
			MessagingInterface::setContext(context);
			Logger::instance();
			Dispatcher::instance();
			MessageLog::setMaxMemory(10000);
			MachineClass *fc = new MachineClass("FLAG");
			flag = MachineInstanceFactory::create("flag", "FLAG");
			std::list<std::string> files;
			loadConfig(files);
			initialise_machines();
		}
	protected:
	    MachineInstance *flag;
	};

	TEST_F(DisableActionTest, DisablesTheMachineIfItExists) {

		DisableActionTemplate dat("flag");
		Action *da = dat.factory(flag);
		ASSERT_NE(da, nullptr);
		ASSERT_TRUE(flag->enabled()) << "The machine is initially enabled";
		Action::Status status = (*da)();
		ASSERT_EQ(Action::Status::Complete, status) << "The command completes";
		ASSERT_FALSE(flag->enabled()) << "The command disabled the machine";
		delete da;
	}

	TEST_F(DisableActionTest, FailsIfTheMachineDoesNotExist) {

		DisableActionTemplate dat("not-a-machine");
		Action *da = dat.factory(flag);
		ASSERT_NE(da, nullptr);
		ASSERT_TRUE(flag->enabled()) << "The machine is initially enabled";;
		Action::Status status = (*da)();
		ASSERT_EQ(Action::Status::Failed, status) << "The command completes with a Failed status";;
		delete da;
	}

}
