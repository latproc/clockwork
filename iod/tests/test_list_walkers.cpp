#include "clockwork.h"
#include "Dispatcher.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "Message.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "Scheduler.h"
#include "ThreadSafeQueue.h"
#include "library_globals.cpp"
#include "gtest/gtest.h"

#include <cstdlib>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#ifndef LIST_WALKER_FIXTURE_DIR
#error LIST_WALKER_FIXTURE_DIR must be set
#endif

namespace {

const char *kFixDir = LIST_WALKER_FIXTURE_DIR;

MachineInstance *mustFind(const char *name) {
    MachineInstance *m = MachineInstance::find(name);
    EXPECT_NE(m, nullptr) << name;
    return m;
}

std::string stateOf(const char *name) {
    MachineInstance *m = mustFind(name);
    if (!m) {
        return "";
    }
    return m->getCurrentStateVal()->asString();
}

std::string valueOf(const char *name, const char *prop = "VALUE") {
    MachineInstance *m = mustFind(name);
    if (!m) {
        return "";
    }
    return m->getValue(prop).asString();
}

int64_t intValue(const char *name, const char *prop) {
    MachineInstance *m = mustFind(name);
    if (!m) {
        return 0;
    }
    int64_t v = 0;
    m->getValue(prop).asInteger(v);
    return v;
}

int countState(const std::vector<const char *> &names, const char *want) {
    int n = 0;
    for (const char *name : names) {
        if (stateOf(name) == want) {
            ++n;
        }
    }
    return n;
}

void sendCmd(const char *name, const char *cmd) {
    MachineInstance *m = mustFind(name);
    ASSERT_NE(m, nullptr);
    m->execute(Message(cmd), m);
}

void drainSchedulerDue() {
    Scheduler *sched = Scheduler::instance();
    for (int i = 0; i < 32; ++i) {
        uint64_t now = 0;
        {
            struct timeval tv;
            gettimeofday(&tv, nullptr);
            now = static_cast<uint64_t>(tv.tv_sec) * 1000000ULL +
                  static_cast<uint64_t>(tv.tv_usec);
        }
        if (!sched->ready(now)) {
            return;
        }
        ScheduledItem *item = sched->next();
        if (!item) {
            return;
        }
        if (item->action) {
            (*item->action)();
        }
        sched->pop();
        delete item;
    }
}

void finishActions(MachineInstance *m) {
    for (int n = 0; n < 16; ++n) {
        if (!m->hasMail() && !m->executingCommand()) {
            return;
        }
        m->idle();
    }
}

void stepMachine(MachineInstance *m) {
    if (!m || !m->enabled()) {
        return;
    }
    finishActions(m);
    if (!m->executingCommand() && m->getStateMachine() &&
        m->getStateMachine()->allow_auto_states) {
        m->setNeedsCheck();
        if (m->setStableState()) {
            finishActions(m);
        }
    }
}

void drainOnce() {
    for (int pass = 0; pass < 8; ++pass) {
        for (auto it = MachineInstance::begin(); it != MachineInstance::end(); ++it) {
            stepMachine(*it);
        }
        drainSchedulerDue();
    }
}

void drainSamePass() {
    drainOnce();
    drainOnce();
}

class ListWalkersTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        zmq::context_t *ctx = new zmq::context_t;
        MessagingInterface::setContext(ctx);
        Logger::instance();
        MessageLog::setMaxMemory(20000);
        static boost::condition_variable_any cond;
        static boost::shared_mutex mutex;
        static SharedThreadSafeQueue<Package *> queue(cond, mutex);
        Dispatcher::create(queue);

        std::list<std::string> files;
        auto maybe = [&](const std::string &p) {
            struct stat st;
            if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                files.push_back(p);
            }
        };
        maybe(std::string(kFixDir) + "/systemexec_stub.lpc");
        maybe(std::string(kFixDir) + "/list_walkers_types.lpc");
        maybe(std::string(kFixDir) + "/grab_planner_stub.lpc");
        files.push_back(std::string(kFixDir) + "/list_walkers.lpc");

        ASSERT_EQ(loadConfig(files), 0) << "LPC load failed (see stderr)";
        ASSERT_TRUE(initialise_machines());

        for (auto it = MachineInstance::begin(); it != MachineInstance::end(); ++it) {
            MachineInstance *m = *it;
            if (m && !m->enabled()) {
                m->enable();
            }
            if (m) {
                m->setNeedsCheck();
            }
        }
        drainSamePass();
        drainSamePass();

        MachineInstance *mode = MachineInstance::find("P_Mode");
        ASSERT_NE(mode, nullptr);
        mode->execute(Message("turnOn"), mode);
        drainSamePass();
        drainSamePass();
    }

    void resetScreen() {
        sendCmd("M_HoldA", "Reset");
        sendCmd("M_HoldB", "Reset");
        sendCmd("M_HoldC", "Reset");
        sendCmd("G_Fault", "pass");
        sendCmd("P_Mode", "turnOn");
        sendCmd("M_Screen", "Reset");
        drainSamePass();
    }
};

TEST_F(ListWalkersTest, PrimitiveMoveAndSendTurnOnSameDrain) {
    sendCmd("F_ItemA", "turnOff");
    sendCmd("F_ItemB", "turnOff");
    sendCmd("F_ItemC", "turnOff");
    drainSamePass();

    sendCmd("M_ListOps", "light");
    drainOnce();
    EXPECT_EQ(stateOf("F_ItemA"), "on");
    EXPECT_EQ(stateOf("F_ItemB"), "on");
    EXPECT_EQ(stateOf("F_ItemC"), "on");

    sendCmd("M_ListOps", "peel");
    drainOnce();
    MachineInstance *src = mustFind("L_Items");
    MachineInstance *work = mustFind("L_Moved");
    ASSERT_NE(src, nullptr);
    ASSERT_NE(work, nullptr);
    EXPECT_EQ(src->parameters.size(), 2u);
    EXPECT_EQ(work->parameters.size(), 1u);
}

TEST_F(ListWalkersTest, ScreenWalkLandsOnAutoHome) {
    resetScreen();
    drainOnce();
    EXPECT_EQ(stateOf("G_HoldsClear"), "true");
    EXPECT_EQ(valueOf("V_Screen"), "AutoHome")
        << "walker=" << stateOf("M_Screen")
        << " track=" << stateOf("M_ScreenTrack");
}

TEST_F(ListWalkersTest, ScreenHoldQuestionStaysUntilAnswered) {
    resetScreen();
    sendCmd("M_HoldA", "Question");
    drainSamePass();
    EXPECT_EQ(stateOf("M_HoldA"), "ask");
    EXPECT_EQ(stateOf("G_HoldA"), "false");
    EXPECT_EQ(stateOf("G_HoldsClear"), "false");
    EXPECT_EQ(valueOf("V_Screen"), "HoldA") << "walker=" << stateOf("M_Screen");
}

TEST_F(ListWalkersTest, ScreenHoldYesReturnsToAutoHomeSameDrain) {
    resetScreen();
    sendCmd("M_HoldA", "Question");
    drainSamePass();
    ASSERT_EQ(valueOf("V_Screen"), "HoldA");

    sendCmd("M_HoldA", "Yes");
    drainOnce();
    EXPECT_EQ(stateOf("M_HoldA"), "on");
    EXPECT_EQ(stateOf("G_HoldA"), "true");
    EXPECT_EQ(stateOf("G_HoldsClear"), "true");
    EXPECT_EQ(valueOf("V_Screen"), "AutoHome")
        << "walker=" << stateOf("M_Screen")
        << " (tracker must take last Jump in this drain)";
}

TEST_F(ListWalkersTest, ScreenForkChangeCopiesManualList) {
    resetScreen();
    drainSamePass();
    ASSERT_EQ(valueOf("V_Screen"), "AutoHome");

    sendCmd("P_Mode", "turnOff");
    sendCmd("M_Screen", "Reset");
    drainOnce();
    EXPECT_EQ(stateOf("G_Mode"), "false");
    EXPECT_EQ(valueOf("V_Screen"), "ManualHome")
        << "walker=" << stateOf("M_Screen")
        << " fork=" << stateOf("M_ScreenFork");
}

TEST_F(ListWalkersTest, RotateMapOddCountTurnsOnThreeCells) {
    sendCmd("M_Rotate", "clear");
    drainSamePass();

    MachineInstance *ctl = mustFind("M_Rotate");
    ASSERT_NE(ctl, nullptr);
    ctl->setValue("count", 3);
    sendCmd("M_Rotate", "start");
    usleep(15000);
    drainSamePass();
    drainSamePass();

    const std::vector<const char *> cells = {"F_Cell1", "F_Cell2", "F_Cell3",
                                             "F_Cell4", "F_Cell5", "F_Cell6"};
    EXPECT_EQ(countState(cells, "on"), 3) << "ctl=" << stateOf("M_Rotate");
    EXPECT_NE(intValue("M_Rotate", "bits"), 0);
}

TEST_F(ListWalkersTest, RotateMapEvenCountSelectsTwoCells) {
    sendCmd("M_Rotate", "clear");
    drainSamePass();

    MachineInstance *ctl = mustFind("M_Rotate");
    ASSERT_NE(ctl, nullptr);
    ctl->setValue("count", 2);
    sendCmd("M_Rotate", "start");
    usleep(15000);
    drainSamePass();
    drainSamePass();

    const std::vector<const char *> cells = {"F_Cell1", "F_Cell2", "F_Cell3",
                                             "F_Cell4", "F_Cell5", "F_Cell6"};
    EXPECT_EQ(countState(cells, "on"), 2)
        << "ctl=" << stateOf("M_Rotate") << " bits=" << intValue("M_Rotate", "bits");
    EXPECT_NE(intValue("M_Rotate", "bits"), 0);
}

TEST_F(ListWalkersTest, GridMapSkipsDeselectedSlot) {
    sendCmd("M_Grid", "clear");
    sendCmd("P_Skip0", "turnOn");
    sendCmd("P_Skip1", "turnOff");
    drainSamePass();

    MachineInstance *ctl = mustFind("M_Grid");
    ASSERT_NE(ctl, nullptr);
    ctl->setValue("count", 2);
    sendCmd("M_Grid", "start");
    drainSamePass();
    drainSamePass();
    drainSamePass();

    EXPECT_EQ(stateOf("F_R1S0"), "off");
    EXPECT_EQ(stateOf("F_R2S0"), "off");
    EXPECT_EQ(stateOf("F_R3S0"), "off");
    int on = countState({"F_R1S1", "F_R2S1", "F_R3S1"}, "on");
    EXPECT_EQ(on, 2) << "ctl=" << stateOf("M_Grid")
                     << " bits=" << intValue("M_Grid", "bits");
    EXPECT_NE(intValue("M_Grid", "bits"), 0);
}

} // namespace
