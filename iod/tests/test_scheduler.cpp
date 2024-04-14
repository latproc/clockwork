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
#include <process.h>

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
        scheduler.resume();
    }
    EXPECT_EQ(up_counter, down_counter);
}

