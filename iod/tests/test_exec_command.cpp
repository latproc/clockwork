#include "cw_test.h"
#include <ControlSystemMachine.h>
#include <Expression.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <ProcessingThread.h>
#include <SetStateAction.h>
#include <debug_malloc.h>
#include <exec_command.h>
#include <symboltable.h>

#include "Statistics.h"
#include <Dispatcher.h>
#include <Logger.h>
#include <MessagingInterface.h>
#include <iostream>
#include <zmq.hpp>
#include <ThreadSafeQueue.h>
#include <MQTTInterface.h>
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
        EXPECT_TRUE(machine_class_ != nullptr);
        machine_class_->addState("Done", true);
        machine_class_->addState("Error", true);
        machine_class_->addState("Running", true);
        machine_class_->addState("Init", true);
        machine_class_->addState("Start", true);
        machine_class_->default_state = State("Init");
        machine_class_->initial_state = State("Init");

        MachineInstance *one = MachineInstanceFactory::create("one", machine_class_->name);
        one->setStateMachine(machine_class_);
        one->addLocal(Value{"test"}, scope_);
        one->addDependancy(scope_);
        one->properties.add("Command", Value{"/bin/ls"});
        one->properties.add("CommandStatus", Value{(uint64_t)0});
        one->properties.add("Result", Value{""});
        one->properties.add("Errors", Value{""});
        MoveStateActionTemplate msat("one", Value{"Start"});
        MoveStateAction *msa = static_cast<MoveStateAction *>(msat.factory(one));
        one->enable();
        msa->start();
        msa->run();
        one->idle();
        EXPECT_TRUE(one->getCurrentStateVal() != nullptr);
        EXPECT_TRUE(*one->getCurrentStateVal() == "Start");
        exec_command((void *)one);
        EXPECT_TRUE(*one->getCurrentStateVal() != Value{"Error"});
        auto timer = microsecs();
        while (one->getCurrentStateVal() != nullptr && *one->getCurrentStateVal() != Value{"Done"}) {
            usleep(10000);
            one->idle();
            std::cout << "current state: " << *one->getCurrentStateVal() << "\n";
            exec_command((void *)one);
            EXPECT_TRUE(microsecs() - timer < 1000000);
        }
        Value res = one->getValue("CommandStatus");
        std::cout << "CommandStatus: " << res << "\n";
        EXPECT_INT(res);
        EXPECT_TRUE(res == SymbolTable::Zero);
        res = *one->getCurrentStateVal();
        int count = 0;
        while (res != Value{"Done"} && ++count < 10) {
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
    boost::condition_variable_any refresh_cond_var;
    boost::shared_mutex refresh_mutex;
    SharedThreadSafeQueue<MachineInstance*> refresh_queue(refresh_cond_var, refresh_mutex);
    boost::condition_variable_any mqtt_cond_var;
    boost::shared_mutex mqtt_mutex;
    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> mqtt_queue(mqtt_cond_var, mqtt_mutex);
    Dispatcher::create(queue);
    Logger::instance();
    zmq::socket_t dispatch_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    dispatch_sync.connect("inproc://dispatcher_sync");
    ControlSystemMachine machine;
    IODCommandThread *stateMonitor = IODCommandThread::instance();
    IODHardwareActivation iod_activation;
    ProcessingThread &processMonitor(
        ProcessingThread::create(machine, iod_activation, *stateMonitor, queue, refresh_queue, mqtt_queue));
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
