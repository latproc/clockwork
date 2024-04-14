#include "gtest/gtest.h"
#include <symboltable.h>
#include <value.h>
#include <cJSON.h>
#include <boost/context/fiber.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <json_expr_parser.h>
#include <json_expression.h>
#include <vector>
#include <deque>

namespace Process {

struct  Proc {
    static size_t next_id; // Unique identifier for an instance of a Proc
    size_t id = next_id++;
    boost::context::fiber f;
    bool ready = true;
    Proc(boost::context::fiber && f_) : f(std::move(f_)) {}
    bool operator==(const Proc &other) const { return id == other.id; }
};

size_t Proc::next_id = 0;
Proc *proc = nullptr;

class Scheduler;
namespace ctx = boost::context;

class Scheduler {
public:
    using Task = boost::context::fiber;
    Scheduler();

    void run(std::deque<Proc> &procs, bool &done) {
        sch = std::move(sch).resume(); // start the scheduler
    }
    void add(Proc && p) {
        all_procs.push_back(std::move(p));
        runnable.push_back(&all_procs.back());
    }
    void start() { run(all_procs, done); }
    bool finished() { return all_procs.empty(); }
    void stop() { done = true; }
    void next() { sch = std::move(sch).resume(); }
    void activate(Proc *p);
    void join(); // Block the caller until all procs are finished.

private:
    std::deque<Proc> all_procs; 
    std::deque<Proc*> runnable; // round robin scheduler
    bool done = false;

    ctx::fiber sch;
};

Scheduler::Scheduler() : 
    sch([&](ctx::fiber && main) {
        while (!done) {
            if (runnable.empty()) {
                main = std::move(main).resume(); // resume main
                continue;
            }
            // TODO: Use a circular buffer for the runnable queue.
            proc = runnable.front();
            runnable.pop_front();
            if (proc->ready && proc->f) {
                proc->f = std::move(proc->f).resume();
            }
            if (proc->f) {
                runnable.push_back(proc);
            }
            else {
                auto found = std::find(all_procs.begin(), all_procs.end(), *proc);
                if (found != all_procs.end()) {
                    all_procs.erase(found);
                }
            }
            main = std::move(main).resume(); // resume main
        }
        return std::move(main);
    })
{
}
void Scheduler::activate(Proc *p) {
    // Some procs may have been added or have become ready.
    for (auto & proc : all_procs) {
        if (*p == proc) {
            assert(proc.ready);
            runnable.push_front(&proc); // Signalled procs run immediately
            next();
            return;
        }
    }
    assert("Attempt to activate an unknown proc" && false);
}

void Scheduler::join() {
    while (!finished()) {
        next();
    }
}

class Signal {
public:
    Signal(Scheduler &scheduler_) : scheduler(scheduler_) {}
    void wait(Scheduler::Task && sink);
    void send();
    bool awaited();
private:
    std::deque<Proc*> procs; // Procs waiting for this signal
    Scheduler &scheduler;
};

void Signal::wait(Scheduler::Task && sink) {
    if (proc) {
        procs.push_back(proc);
        proc->ready = false;
        sink = std::move(sink).resume();
    }
}

void Signal::send() {
    if (!procs.empty()) {
        auto proc = procs.front();
        procs.pop_front();
        proc->ready = true;
        scheduler.activate(proc);
    }
}

bool Signal::awaited() {
    return !procs.empty();
}

} // namespace Process

TEST(Scheduler, SchedulerRunsOneTask) {
    Process::Scheduler scheduler;

    bool ran = false;
    Process::Proc p(Process::Scheduler::Task([&ran](Process::Scheduler::Task && sink) {
        ran = true;
        return std::move(sink);
    }));
    scheduler.add(std::move(p));
    scheduler.start();
    scheduler.join();
    EXPECT_TRUE(scheduler.finished()); // Proc p finishes immediately
    EXPECT_TRUE(ran);
}

TEST(Scheduler, SchedulerRunsMultipleTasks) {
    Process::Scheduler scheduler;
    scheduler.start();

    bool ran_a = false, ran_b = false;
    Process::Proc a(Process::Scheduler::Task([&ran_a](Process::Scheduler::Task && sink) {
        ran_a = true;
        return std::move(sink);
    }));
    Process::Proc b(Process::Scheduler::Task([&ran_b](Process::Scheduler::Task && sink) {
        ran_b = true;
        return std::move(sink);
    }));
    scheduler.add(std::move(a));
    scheduler.add(std::move(b));
    scheduler.join();
    EXPECT_TRUE(scheduler.finished());
    EXPECT_TRUE(ran_a && ran_b);
}

TEST(Scheduler, ScheduledTasksDestructLocalVariables) {
    Process::Scheduler scheduler;

    bool cleaned_up = false;
    struct Cleanup {
        bool &ran;
        Cleanup(bool &ran_) : ran(ran_) {}
        ~Cleanup() { ran = true; }
    };

    Process::Proc p(Process::Scheduler::Task([&cleaned_up](Process::Scheduler::Task && sink) {
        Cleanup cleanup(cleaned_up);
        return std::move(sink);
    }));
    scheduler.add(std::move(p));
    scheduler.start();
    scheduler.join();
    EXPECT_TRUE(scheduler.finished());
    EXPECT_TRUE(cleaned_up);
}

TEST(Scheduler, TasksCanWaitForSignals) {
    Process::Scheduler scheduler;
    scheduler.start(); // The scheduler can be started here or after adding tasks.

    int down_counter = 5, up_counter = 0;

    Process::Proc count_up(Process::Scheduler::Task([&](Process::Scheduler::Task && sink) {
        sink = std::move(sink).resume();
        while (count_up.ready) {
            ++up_counter;
            sink = std::move(sink).resume();
        };
        return std::move(sink);
    }));

    Process::Proc count_down(Process::Scheduler::Task([&](Process::Scheduler::Task && sink) {
        sink = std::move(sink).resume();
        while (count_down.ready) {
            --down_counter;
            sink = std::move(sink).resume();
        };
        return std::move(sink);
    }));

    Process::Signal counters_match(scheduler);

    Process::Proc stop_when_equal(Process::Scheduler::Task([&](Process::Scheduler::Task && sink) {
        counters_match.wait(std::move(sink));
        assert(up_counter == down_counter);
        count_up.ready = false;
        count_down.ready = false;
        return std::move(sink);
    }));

    scheduler.add(std::move(count_up));
    scheduler.add(std::move(count_down));
    scheduler.add(std::move(stop_when_equal));

    while (!scheduler.finished()) {
        if (up_counter == down_counter) {
            counters_match.send();
        }
        scheduler.next();
    }
    EXPECT_EQ(up_counter, down_counter);
}

