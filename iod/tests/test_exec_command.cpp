#include "cw_test.h"
#include <ControlSystemMachine.h>
#include <DebugExtra.h>
#include <Dispatcher.h>
#include <Expression.h>
#include <Logger.h>
#include <MQTTInterface.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <MessagingInterface.h>
#include <ProcessingThread.h>
#include <SetStateAction.h>
#include <Statistics.h>
#include <ThreadSafeQueue.h>
#include <boost/thread.hpp>
#include <debug_malloc.h>
#include <exec_command.h>
#include <symboltable.h>

#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>
#include <zmq.hpp>

bool program_done = false;
bool machine_is_ready = false;
Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;

class IODHardwareActivation : public HardwareActivation {
  public:
    void operator()() override {}
    bool initialiseHardware() override { return true; }
};

static void ensure_states(MachineClass *mc) {
    mc->addState("Done", true);
    mc->addState("Error", true);
    mc->addState("Running", true);
    mc->addState("Idle", true);
    mc->addState("Start", true);
    mc->default_state = State("Idle");
    mc->initial_state = State("Idle");
}

static void pump_until_terminal(MachineInstance *one, int max_iters = 500) {
    for (int i = 0; i < max_iters; ++i) {
        one->idle();
        exec_command((void *)one);
        Value s = *one->getCurrentStateVal();
        if (s == "Done" || s == "Error") {
            return;
        }
        usleep(5000);
    }
}

static void go_start(MachineInstance *one, const char *name) {
    MoveStateActionTemplate msat(name, "Start");
    MoveStateAction *msa = static_cast<MoveStateAction *>(msat.factory(one));
    one->enable();
    msa->start();
    msa->run();
    one->idle();
    exec_command((void *)one);
}

class ExecuteTests {
  public:
    ExecuteTests() : machine_class_{new MachineClass("ExprTest")} {
        machine_class_->setOption("x", 7);
        scope_ = MachineInstanceFactory::create("test", machine_class_->name);
        scope_->setStateMachine(machine_class_);
        ensure_states(machine_class_);

        // Keep all MachineInstances alive for the suite: ProcessingThread may still
        // walk the factory table, and delete races with that thread under load.
        tests_.push_back(TestCase([this]() { return test_basic_success(); }));
        tests_.push_back(TestCase([this]() { return test_capture_result(); }));
        tests_.push_back(TestCase([this]() { return test_failed_command(); }));
        tests_.push_back(TestCase([this]() { return test_empty_command_then_recover(); }));
        tests_.push_back(TestCase([this]() { return test_quoted_args_like_plant(); }));
        tests_.push_back(TestCase([this]() { return test_restart_within_done(); }));
        tests_.push_back(TestCase([this]() { return test_return_codes(); }));
        tests_.push_back(TestCase([this]() { return test_stdout_sizes(); }));
        tests_.push_back(TestCase([this]() { return test_stderr_sizes_and_rc(); }));
        tests_.push_back(TestCase([this]() { return test_stdout_and_stderr_together(); }));
        tests_.push_back(TestCase([this]() { return test_memory_large_payload_loop(); }));
        tests_.push_back(TestCase([this]() { return test_curl_http_ok(); }));
        tests_.push_back(TestCase([this]() { return test_curl_http_code_and_fail(); }));
        tests_.push_back(TestCase([this]() { return test_load_sequential(); }));
        tests_.push_back(TestCase([this]() { return test_load_parallel_instances(); }));
    }
    ~ExecuteTests() {
        for (auto *m : keep_) {
            delete m;
        }
        delete scope_;
    }
    std::list<TestCase> tests() { return tests_; }

  private:
    std::list<TestCase> tests_;
    MachineClass *machine_class_;
    MachineInstance *scope_;
    std::vector<MachineInstance *> keep_;

    MachineInstance *make_one(const char *name) {
        MachineInstance *one = MachineInstanceFactory::create(name, machine_class_->name);
        one->setStateMachine(machine_class_);
        one->addLocal("test", scope_);
        one->addDependancy(scope_);
        one->properties.add("Command", "");
        one->properties.add("CommandStatus", 0);
        one->properties.add("Result", "");
        one->properties.add("Errors", "");
        one->enable();
        keep_.push_back(one);
        return one;
    }

    TestResult test_basic_success() {
        MachineInstance *one = make_one("sys_basic");
        one->properties.add("Command", "/bin/sleep 0.05");
        go_start(one, "sys_basic");
        pump_until_terminal(one);
        Value state = *one->getCurrentStateVal();
        Value status = one->getValue("CommandStatus");
        std::cout << "basic: state=" << state << " status=" << status << "\n" << std::flush;
        EXPECT_TRUE(state == "Done");
        EXPECT_TRUE(status == 0);
        PASS;
    }

    TestResult test_capture_result() {
        MachineInstance *one = make_one("sys_result");
        one->properties.add("Command", "/bin/echo plant-ok");
        go_start(one, "sys_result");
        pump_until_terminal(one);
        Value state = *one->getCurrentStateVal();
        Value result = one->getValue("Result");
        std::cout << "result: state=" << state << " Result=" << result << "\n" << std::flush;
        EXPECT_TRUE(state == "Done");
        EXPECT_TRUE(result.asString().find("plant-ok") != std::string::npos);
        PASS;
    }

    TestResult test_failed_command() {
        MachineInstance *one = make_one("sys_fail");
        one->properties.add("Command", "/bin/false");
        go_start(one, "sys_fail");
        pump_until_terminal(one);
        Value state = *one->getCurrentStateVal();
        Value status = one->getValue("CommandStatus");
        std::cout << "fail: state=" << state << " status=" << status << "\n" << std::flush;
        EXPECT_TRUE(state == "Error");
        EXPECT_TRUE(status != 0);
        PASS;
    }

    TestResult test_empty_command_then_recover() {
        MachineInstance *one = make_one("sys_empty");
        one->properties.add("Command", "");
        go_start(one, "sys_empty");
        pump_until_terminal(one);
        Value state = *one->getCurrentStateVal();
        std::cout << "empty: state=" << state << "\n" << std::flush;
        EXPECT_TRUE(state == "Error");

        one->properties.add("Command", "/bin/echo recovered");
        go_start(one, "sys_empty");
        pump_until_terminal(one);
        state = *one->getCurrentStateVal();
        Value result = one->getValue("Result");
        std::cout << "empty+recover: state=" << state << " Result=" << result << "\n" << std::flush;
        EXPECT_TRUE(state == "Done");
        EXPECT_TRUE(result.asString().find("recovered") != std::string::npos);
        PASS;
    }

    TestResult test_quoted_args_like_plant() {
        MachineInstance *one = make_one("sys_quote");
        one->properties.add("Command", "/bin/echo '12.3 kg'");
        go_start(one, "sys_quote");
        pump_until_terminal(one);
        Value state = *one->getCurrentStateVal();
        Value result = one->getValue("Result");
        std::cout << "quote: state=" << state << " Result=" << result << "\n" << std::flush;
        EXPECT_TRUE(state == "Done");
        EXPECT_TRUE(result.asString().find("12.3 kg") != std::string::npos);
        PASS;
    }

    TestResult test_restart_within_done() {
        MachineInstance *one = make_one("sys_restart");
        one->properties.add("Command", "/bin/echo first");
        go_start(one, "sys_restart");
        pump_until_terminal(one);
        EXPECT_TRUE(*one->getCurrentStateVal() == "Done");

        one->properties.add("Command", "/bin/echo second");
        go_start(one, "sys_restart");
        pump_until_terminal(one);
        Value state = *one->getCurrentStateVal();
        Value result = one->getValue("Result");
        std::cout << "restart: state=" << state << " Result=" << result << "\n" << std::flush;
        EXPECT_TRUE(state == "Done");
        EXPECT_TRUE(result.asString().find("second") != std::string::npos);
        PASS;
    }

    // Explicit exit codes (0 success → Done; non-zero → Error + CommandStatus).
    TestResult test_return_codes() {
        MachineInstance *one = make_one("sys_rc");
        struct Case {
            int code;
            bool expect_done;
        };
        const Case cases[] = {{0, true}, {1, false}, {2, false}, {7, false}, {127, false}};
        for (const auto &c : cases) {
            char cmd[80];
            snprintf(cmd, sizeof(cmd), "/bin/sh -c 'exit %d'", c.code);
            one->properties.add("Command", cmd);
            go_start(one, "sys_rc");
            pump_until_terminal(one);
            Value state = *one->getCurrentStateVal();
            Value status = one->getValue("CommandStatus");
            std::cout << "rc " << c.code << ": state=" << state << " status=" << status << "\n"
                      << std::flush;
            if (c.expect_done) {
                EXPECT_TRUE(state == "Done");
                EXPECT_TRUE(status == 0);
            }
            else {
                EXPECT_TRUE(state == "Error");
                EXPECT_TRUE(status == c.code);
            }
        }
        PASS;
    }

    // Different stdout sizes captured into Result.
    TestResult test_stdout_sizes() {
        MachineInstance *one = make_one("sys_out_sz");
        const int sizes[] = {0, 1, 64, 1024, 8192, 65536};
        for (int n : sizes) {
            char cmd[256];
            // python is reliable for exact byte counts without shell metachar issues
            snprintf(cmd, sizeof(cmd),
                     "/usr/bin/python3 -c \"import sys; sys.stdout.write('A'*%d)\"", n);
            one->properties.add("Command", cmd);
            go_start(one, "sys_out_sz");
            pump_until_terminal(one, 2000);
            Value state = *one->getCurrentStateVal();
            Value result = one->getValue("Result");
            std::string r = result.asString();
            // strip trailing newline if any (python write has none)
            while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) {
                r.pop_back();
            }
            std::cout << "stdout size want=" << n << " got=" << r.size() << " state=" << state
                      << "\n"
                      << std::flush;
            EXPECT_TRUE(state == "Done");
            EXPECT_TRUE(static_cast<int>(r.size()) == n);
            if (n > 0) {
                EXPECT_TRUE(r.find_first_not_of('A') == std::string::npos);
            }
        }
        PASS;
    }

    // stderr sizes + non-zero exit (plant often cares about Errors + CommandStatus).
    TestResult test_stderr_sizes_and_rc() {
        MachineInstance *one = make_one("sys_err_sz");
        const int sizes[] = {0, 1, 256, 4096, 32000};
        for (int n : sizes) {
            char cmd[320];
            snprintf(cmd, sizeof(cmd),
                     "/usr/bin/python3 -c \"import sys; sys.stderr.write('E'*%d); sys.exit(3)\"",
                     n);
            one->properties.add("Command", cmd);
            go_start(one, "sys_err_sz");
            pump_until_terminal(one, 2000);
            Value state = *one->getCurrentStateVal();
            Value status = one->getValue("CommandStatus");
            Value errors = one->getValue("Errors");
            std::string e = errors.asString();
            while (!e.empty() && (e.back() == '\n' || e.back() == '\r')) {
                e.pop_back();
            }
            std::cout << "stderr size want=" << n << " got=" << e.size() << " state=" << state
                      << " status=" << status << "\n"
                      << std::flush;
            EXPECT_TRUE(state == "Error");
            EXPECT_TRUE(status == 3);
            EXPECT_TRUE(static_cast<int>(e.size()) == n);
            if (n > 0) {
                EXPECT_TRUE(e.find_first_not_of('E') == std::string::npos);
            }
        }
        PASS;
    }

    // Both streams populated on failure.
    TestResult test_stdout_and_stderr_together() {
        MachineInstance *one = make_one("sys_both");
        one->properties.add(
            "Command",
            "/usr/bin/python3 -c \"import sys; sys.stdout.write('OUTDATA'); "
            "sys.stderr.write('ERRDATA'); sys.exit(9)\"");
        go_start(one, "sys_both");
        pump_until_terminal(one);
        Value state = *one->getCurrentStateVal();
        Value status = one->getValue("CommandStatus");
        Value result = one->getValue("Result");
        Value errors = one->getValue("Errors");
        std::cout << "both: state=" << state << " status=" << status << " Result=" << result
                  << " Errors=" << errors << "\n"
                  << std::flush;
        EXPECT_TRUE(state == "Error");
        EXPECT_TRUE(status == 9);
        EXPECT_TRUE(result.asString().find("OUTDATA") != std::string::npos);
        EXPECT_TRUE(errors.asString().find("ERRDATA") != std::string::npos);
        PASS;
    }

    // Memory: large payloads repeatedly; debug_malloc balance must not climb.
    TestResult test_memory_large_payload_loop() {
        MachineInstance *one = make_one("sys_mem");
        const int rounds = 25;
        const int payload = 50000; // 50 KiB stdout each round
        int before = debug_mallocs_remaining();
        int ok = 0;
        for (int i = 0; i < rounds; ++i) {
            char cmd[256];
            snprintf(cmd, sizeof(cmd),
                     "/usr/bin/python3 -c \"import sys; sys.stdout.write('M'*%d)\"", payload);
            one->properties.add("Command", cmd);
            go_start(one, "sys_mem");
            pump_until_terminal(one, 3000);
            if (*one->getCurrentStateVal() == "Done" &&
                static_cast<int>(one->getValue("Result").asString().size()) >= payload) {
                ++ok;
            }
        }
        int after = debug_mallocs_remaining();
        std::cout << "mem loop: " << ok << "/" << rounds << " ok"
                  << " mallocs before=" << before << " after=" << after
                  << " delta=" << (after - before) << "\n"
                  << std::flush;
        EXPECT_TRUE(ok == rounds);
        // Allow a small fixed harness residual; reject unbounded growth.
        EXPECT_TRUE((after - before) < 16);
        PASS;
    }

    // curl happy path (network): body lands in Result, exit 0 → Done.
    TestResult test_curl_http_ok() {
        MachineInstance *one = make_one("sys_curl_ok");
        one->properties.add(
            "Command",
            "/usr/bin/curl -sS --max-time 15 -L https://example.com");
        go_start(one, "sys_curl_ok");
        pump_until_terminal(one, 4000);
        Value state = *one->getCurrentStateVal();
        Value status = one->getValue("CommandStatus");
        Value result = one->getValue("Result");
        std::cout << "curl ok: state=" << state << " status=" << status
                  << " body_len=" << result.asString().size() << "\n"
                  << std::flush;
        EXPECT_TRUE(state == "Done");
        EXPECT_TRUE(status == 0);
        EXPECT_TRUE(result.asString().size() > 50);
        EXPECT_TRUE(result.asString().find("Example Domain") != std::string::npos ||
                    result.asString().find("example") != std::string::npos ||
                    result.asString().find("Example") != std::string::npos);
        PASS;
    }

    // curl: print HTTP code on stdout; fail host → non-zero + Errors often has curl text.
    TestResult test_curl_http_code_and_fail() {
        MachineInstance *one = make_one("sys_curl_code");
        one->properties.add(
            "Command",
            "/usr/bin/curl -sS --max-time 15 -o /dev/null -w %{http_code} https://example.com");
        go_start(one, "sys_curl_code");
        pump_until_terminal(one, 4000);
        Value state = *one->getCurrentStateVal();
        Value status = one->getValue("CommandStatus");
        Value result = one->getValue("Result");
        std::string code = result.asString();
        while (!code.empty() && (code.back() == '\n' || code.back() == '\r')) {
            code.pop_back();
        }
        std::cout << "curl http_code: state=" << state << " status=" << status
                  << " Result='" << code << "'\n"
                  << std::flush;
        EXPECT_TRUE(state == "Done");
        EXPECT_TRUE(status == 0);
        EXPECT_TRUE(code == "200");

        // Connection refused / unreachable: non-zero CommandStatus, Error state
        one->properties.add(
            "Command",
            "/usr/bin/curl -sS --max-time 3 http://127.0.0.1:1/");
        go_start(one, "sys_curl_code");
        pump_until_terminal(one, 2000);
        state = *one->getCurrentStateVal();
        status = one->getValue("CommandStatus");
        Value errors = one->getValue("Errors");
        std::cout << "curl fail: state=" << state << " status=" << status
                  << " Errors_len=" << errors.asString().size() << "\n"
                  << std::flush;
        EXPECT_TRUE(state == "Error");
        EXPECT_TRUE(status != 0);
        PASS;
    }

    TestResult test_load_sequential() {
        const int N = 80;
        MachineInstance *one = make_one("sys_load_seq");
        int ok = 0;
        for (int i = 0; i < N; ++i) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "/bin/echo seq-%d", i);
            one->properties.add("Command", cmd);
            go_start(one, "sys_load_seq");
            pump_until_terminal(one, 1000);
            if (*one->getCurrentStateVal() == "Done") {
                ++ok;
            }
        }
        std::cout << "load sequential: " << ok << "/" << N << " Done"
                  << " mallocs_remaining=" << debug_mallocs_remaining() << "\n"
                  << std::flush;
        EXPECT_TRUE(ok == N);
        EXPECT_TRUE(debug_mallocs_remaining() < 64);
        PASS;
    }

    TestResult test_load_parallel_instances() {
        const int N = 6;
        const int rounds = 15;
        std::vector<MachineInstance *> machines;
        std::vector<std::string> names;
        for (int i = 0; i < N; ++i) {
            char name[32];
            snprintf(name, sizeof(name), "sys_par_%d", i);
            names.push_back(name);
            machines.push_back(make_one(name));
        }
        int ok = 0;
        for (int r = 0; r < rounds; ++r) {
            for (int i = 0; i < N; ++i) {
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "/bin/echo p-%d-%d", i, r);
                machines[i]->properties.add("Command", cmd);
                go_start(machines[i], names[i].c_str());
            }
            for (int step = 0; step < 400; ++step) {
                bool all_done = true;
                for (int i = 0; i < N; ++i) {
                    machines[i]->idle();
                    exec_command((void *)machines[i]);
                    Value s = *machines[i]->getCurrentStateVal();
                    if (!(s == "Done" || s == "Error")) {
                        all_done = false;
                    }
                }
                if (all_done) {
                    break;
                }
                usleep(2000);
            }
            for (int i = 0; i < N; ++i) {
                if (*machines[i]->getCurrentStateVal() == "Done") {
                    ++ok;
                }
            }
        }
        const int expected = N * rounds;
        std::cout << "load parallel: " << ok << "/" << expected << " Done"
                  << " mallocs_remaining=" << debug_mallocs_remaining() << "\n"
                  << std::flush;
        EXPECT_TRUE(ok == expected);
        PASS;
    }
};

int main(int, char **) {
    static Value default_polling_delay(2000);
    MachineInstance::polling_delay = &default_polling_delay;
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    boost::condition_variable_any m_cond_var;
    boost::shared_mutex m_mutex;
    SharedThreadSafeQueue<Package *> queue(m_cond_var, m_mutex);
    boost::condition_variable_any mqtt_cond_var;
    boost::shared_mutex mqtt_mutex;
    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage *> mqtt_queue(mqtt_cond_var,
                                                                          mqtt_mutex);
    Dispatcher::create(queue);
    Logger::instance();
    DebugExtra::instance();
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
    MachineInstance *system = MachineInstanceFactory::create("SYSTEM", "SYSTEMSETTINGS");
    (void)system;

    int result = 0;
    {
        TestRunner tests;
        ExecuteTests execute_tests;
        tests.add(execute_tests.tests());
        auto success_pct = tests.run_all() * 100;
        std::cout << std::fixed << std::setprecision(2) << success_pct << "% passed ("
                  << tests.count() << " cases)\n"
                  << std::flush;
        result = success_pct == 100 ? 0 : 1;
    }

    MessagingInterface::abort();
    Dispatcher::instance()->stop();
    delete Dispatcher::instance();
    LogState::cleanup();
    Logger::cleanup();

    return result;
}
