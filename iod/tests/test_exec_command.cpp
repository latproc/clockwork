#include "cw_test.h"
#include <ControlSystemMachine.h>
#include <Expression.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <ProcessingThread.h>
#include <SetStateAction.h>
#include <debug_malloc.h>
#include <exec_command.h>
#include <memory>
#include <symboltable.h>

#include "Statistics.h"
#include "split_string.h"
#include <Dispatcher.h>
#include <Logger.h>
#include <MessagingInterface.h>
#include <iostream>
#include <zmq.hpp>
#include <ThreadSafeQueue.h>
#include <boost/thread.hpp>

bool program_done = false;
bool machine_is_ready = false;
Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;

class IODHardwareActivation : public HardwareActivation {
  public:
    void operator()() override {
        //initialise_machines();
    }

    bool initialiseHardware() override { return true; }
};

class ExecuteTests {
  public:
    ExecuteTests() : machine_class_{new MachineClass("ExprTest")} {
        machine_class_->setOption("x", 7);
        scope_ = MachineInstanceFactory::create("test", machine_class_->name);
        scope_->setStateMachine(machine_class_);
        tests_.push_back(TestCase([this]() { return test(); }));
    }
    ~ExecuteTests() { delete scope_; }
    std::list<TestCase> tests() { return tests_; }

  private:
    std::list<TestCase> tests_;
    MachineClass *machine_class_;
    MachineInstance *scope_;

    TestResult test() {
        MachineInstance *one = MachineInstanceFactory::create("one", machine_class_->name);
        one->setStateMachine(machine_class_);
        one->addLocal("test", scope_);
        one->addDependancy(scope_);
        machine_class_->addState("Done", true);
        machine_class_->addState("Error", true);
        machine_class_->addState("Running", true);
        machine_class_->addState("Init", true);
        machine_class_->addState("Start", true);
        machine_class_->default_state = State("Init");
        machine_class_->initial_state = State("Init");
        one->properties.add("Command", "/bin/ls");
        one->properties.add("CommandStatus", 0);
        one->properties.add("Result", "");
        one->properties.add("Errors", "");
        MoveStateActionTemplate msat("one", "Start");
        MoveStateAction *msa = static_cast<MoveStateAction *>(msat.factory(one));
        one->enable();
        msa->start();
        msa->run();
        one->idle();
        exec_command((void *)one);
        usleep(10000);
        one->idle();
        exec_command((void *)one);
        Value res = one->getValue("CommandStatus");
        std::cout << "CommandStatus: " << res << "\n";
        EXPECT_INT(res);
        EXPECT_TRUE(res == 0);
        res = *one->getCurrentStateVal();
        int count = 0;
        while (res != "Done" && ++count < 10) {
            usleep(10000);
            res = *one->getCurrentStateVal();
        }
        std::cout << "State: " << res << "\n";
        EXPECT_TRUE(res.kind == Value::t_symbol || res.kind == Value::t_string);
        EXPECT_TRUE(res == "Done");
        if (debug_mallocs_remaining()) {
            std::cout << "mallocs remaining: " << debug_mallocs_remaining() << "\n";
        }
        EXPECT_TRUE(debug_mallocs_remaining() == 0);
        delete one;
        PASS;
    }
};

int main(int, char **) {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    boost::condition_variable_any m_cond_var;
    boost::shared_mutex m_mutex;
    SharedThreadSafeQueue<Package*> queue(m_cond_var, m_mutex);
    Dispatcher::create(queue);
    Logger::instance();
    zmq::socket_t dispatch_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    dispatch_sync.connect("inproc://dispatcher_sync");
    ControlSystemMachine machine;
    IODCommandThread *stateMonitor = IODCommandThread::instance();
    IODHardwareActivation iod_activation;
    ProcessingThread &processMonitor(
        ProcessingThread::create(&machine, iod_activation, *stateMonitor, queue));
    processMonitor.setProcessingThreadInstance(&processMonitor);
    boost::thread process(boost::ref(processMonitor));

    int result = 0;
    {
        TestRunner tests;
        ExecuteTests execute_tests;
        tests.add(execute_tests.tests());
        auto success_pct = tests.run_all() * 100;
        std::cout << std::fixed << std::setprecision(2) << success_pct << "% passed ("
                  << tests.count() << " cases)\n";
        result = success_pct == 100 ? 0 : 1;
    }

    MessagingInterface::abort();
    Dispatcher::instance()->stop();
    delete Dispatcher::instance();
    LogState::cleanup();
    Logger::cleanup();

    return result;
}
