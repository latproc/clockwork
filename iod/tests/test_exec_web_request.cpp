#include "cw_test.h"
#include <ControlSystemMachine.h>
#include <MachineInstance.h>
#include <ProcessingThread.h>
#include <exec_web_request.h>
#include <debug_malloc.h>
#include <Statistics.h>
#include "MessagingInterface.h"
#include "Dispatcher.h"
#include "Logger.h"

#include <iostream>
#include <unistd.h>
#include <httplib.h>

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

static void start_test_server() {
    static httplib::Server svr;

    // Serve a static JSON array
    svr.Get("/api/test", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"([
            {"id": 1, "name": "Alice"},
            {"id": 2, "name": "Bob"}
        ])", "application/json");
    });

    // Minimal POST endpoint: echo the posted JSON body back verbatim
    svr.Post("/api/echo", [](const httplib::Request& req, httplib::Response& res) {
        if (req.body.empty()) {
            res.set_content("{}", "application/json");
        } else {
            res.set_content(req.body, "application/json");
        }
    });

    std::thread([&]() {
        svr.listen("127.0.0.1", 8081);
    }).detach();

    // Give it a moment to start
    usleep(100000);
}

class WebRequestTests {
  public:
    WebRequestTests() : machine_class_{new MachineClass("WebTest")} {
        scope_ = MachineInstanceFactory::create("scope", machine_class_->name);
        // Define states once for the class and disable automatic transitions
        machine_class_->addState("Idle", true);
        machine_class_->addState("Start", true);
        machine_class_->addState("Running", true);
        machine_class_->addState("Done", true);
        machine_class_->addState("Error", true);
        machine_class_->initial_state = State("Idle");
        machine_class_->default_state = State("Idle");
        machine_class_->disableAutomaticStateChanges();

        scope_->setStateMachine(machine_class_);
        tests_.push_back(TestCase([this]() { return test_basic_request(); }));
        tests_.push_back(TestCase([this]() { return test_post_request(); }));
        tests_.push_back(TestCase([this]() { return test_repeated_requests(); }));
    }
    ~WebRequestTests() { delete scope_; }
    std::list<TestCase> tests() { return tests_; }

  private:
    std::list<TestCase> tests_;
    MachineClass *machine_class_;
    MachineInstance *scope_;

    MachineInstance * create_web_request_machine() {
        MachineInstance *req = MachineInstanceFactory::create("req_get", machine_class_->name);
        req->setStateMachine(machine_class_);
        req->properties.add("Request", Value{"http://127.0.0.1:8081/api/test"});
        req->properties.add("Status", Value{0});
        req->properties.add("Result", Value{""});
        req->properties.add("Errors", Value{""});
        return req;
    }

    MachineInstance * create_web_post_machine() {
        MachineInstance *req = MachineInstanceFactory::create("req_post", machine_class_->name);
        req->setStateMachine(machine_class_);
        req->properties.add("Request", Value{"http://127.0.0.1:8081/api/echo"});
        req->properties.add("Status", Value{0});
        req->properties.add("Result", Value{""});
        req->properties.add("Errors", Value{""});
        // Minimal JSON body to exercise POST path; echoed back verbatim by the test server
        req->properties.add("PostData", Value{R"({"hello":true})"});
        return req;
    }

    TestResult test_basic_request() {
        MachineInstance *req = create_web_request_machine();
        req->idle();
        usleep(10000);

        // start state
        changeState(req,"Start");
        // drive plugin until complete
        //bool finished = false;
        //int res;

        EXPECT_TRUE(req->getCurrentStateVal() != nullptr);
        Value st = *req->getCurrentStateVal();
        EXPECT_TRUE(st == "Start");
        exec_web_request((void*)req);
        st = *req->getCurrentStateVal();
        EXPECT_TRUE(*req->getCurrentStateVal() != Value{"Error"});

        while (req->getCurrentStateVal() != nullptr) {
            req->idle();
            exec_web_request((void*)req);
            //finished = res == PLUGIN_COMPLETED || res == PLUGIN_ERROR;
            usleep(10000);
            Value st = *req->getCurrentStateVal();
            if (st == "Done" || st == "Error") break;
        }

        Value state = *req->getCurrentStateVal();
        std::cout << "Final state: " << state << "\n";
        std::cout << "Status: " << req->getValue("Status") << "\n";
        std::cout << "Errors: " << req->getValue("Errors") << "\n";
        std::cout << "Result: " << req->getValue("Result") << "\n";

        EXPECT_TRUE(state == "Done");
        EXPECT_TRUE(debug_mallocs_remaining() == 0);
        delete req;
        PASS;
    }

    TestResult test_post_request() {
        MachineInstance *req = create_web_post_machine();
        req->idle();
        usleep(10000);

        // Start the request
        changeState(req,"Start");
        EXPECT_TRUE(req->getCurrentStateVal() != nullptr);
        Value st = *req->getCurrentStateVal();
        EXPECT_TRUE(st == "Start");

        exec_web_request((void*)req);
        EXPECT_TRUE(*req->getCurrentStateVal() != Value{"Error"});

        // Pump until the plugin finishes
        while (req->getCurrentStateVal() != nullptr) {
            req->idle();
            exec_web_request((void*)req);
            usleep(10000);
            Value s = *req->getCurrentStateVal();
            if (s == "Done" || s == "Error") break;
        }

        Value state = *req->getCurrentStateVal();
        Value status = req->getValue("Status");
        Value result = req->getValue("Result");
        Value errors = req->getValue("Errors");

        EXPECT_TRUE(state == "Done");
        EXPECT_TRUE(status == 200);
        EXPECT_TRUE(result.asString() == R"({"hello":true})");
        EXPECT_TRUE(debug_mallocs_remaining() == 0);
        delete req;
        PASS;
    }

    // Worker-pool / ownership regression: many sequential completions must not
    // leave debug_malloc outstanding or fail to reach Done.
    TestResult test_repeated_requests() {
        const int N = 50;
        for (int i = 0; i < N; ++i) {
            MachineInstance *req = create_web_request_machine();
            req->idle();
            changeState(req, "Start");
            exec_web_request((void *)req);
            int spins = 0;
            while (req->getCurrentStateVal() != nullptr && spins < 500) {
                req->idle();
                exec_web_request((void *)req);
                usleep(5000);
                Value s = *req->getCurrentStateVal();
                if (s == "Done" || s == "Error") {
                    break;
                }
                ++spins;
            }
            Value state = *req->getCurrentStateVal();
            EXPECT_TRUE(state == "Done");
            EXPECT_TRUE(req->getValue("Status") == 200);
            delete req;
        }
        EXPECT_TRUE(debug_mallocs_remaining() == 0);
        PASS;
    }
};

int main() {
    static Value default_polling_delay(2000);
    MachineInstance::polling_delay = &default_polling_delay;
    start_test_server();
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    boost::condition_variable_any m_cond_var;
    boost::shared_mutex m_mutex;
    SharedThreadSafeQueue<Package*> queue(m_cond_var, m_mutex);
    boost::condition_variable_any mqtt_cond_var;
    boost::shared_mutex mqtt_mutex;
    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> mqtt_queue(mqtt_cond_var, mqtt_mutex);
    Dispatcher::create(queue);
    Logger::instance();
    MachineClass *settings_class = new MachineClass("SYSTEMSETTINGS");
    settings_class->setProperty("POLLING_DELAY", 2000);
    zmq::socket_t dispatch_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    dispatch_sync.connect("inproc://dispatcher_sync");
    ControlSystemMachine machine;
    IODCommandThread *stateMonitor = IODCommandThread::instance();
    IODHardwareActivation iod_activation;
    ProcessingThread &processMonitor(
        ProcessingThread::create(&machine, iod_activation, *stateMonitor, queue, mqtt_queue));
    processMonitor.setProcessingThreadInstance(&processMonitor);
    boost::thread process(boost::ref(processMonitor));
    // ProcessingThread requires that there be a SYSTEM machine to set cycle delays.
    MachineInstance *system = MachineInstanceFactory::create("SYSTEM", "SYSTEMSETTINGS");

    int result = 0;
{
    TestRunner tests;
    WebRequestTests wr_tests;
    tests.add(wr_tests.tests());
    auto success_pct = tests.run_all() * 100;
    std::cout << success_pct << "% passed (" << tests.count() << " cases)\n";
    result = success_pct == 100 ? 0 : 1;
}

    MessagingInterface::abort();
    Dispatcher::instance()->stop();
    delete Dispatcher::instance();
    LogState::cleanup();
    Logger::cleanup();

    return result;

}
