#define EC_SIMULATOR
#include "gtest/gtest.h"
#include <DisableAction.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <Statistic.h>
#include <Statistics.h>
#include "iod_mock.h"

#include "library_globals.c"

namespace {
	class DisableActionTest : public ::testing::Test {
		void SetUp() override {
			MachineClass *fc = new MachineClass("FLAG");
			flag = MachineInstanceFactory::create("flag", "FLAG");
            flag->setStateMachine(fc);
            system.activate();
		}
    private:
        MockSystemSetup system;

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
