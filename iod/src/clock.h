#pragma once

#include <stdint.h>
#include <chrono>

class Clock {
    static std::chrono::time_point<std::chrono::steady_clock> sys_start_time;

  public:
    static uint64_t clock() {
        using namespace std::chrono;
        auto delta = steady_clock::now() - sys_start_time;
        return duration_cast<microseconds>(delta).count();
    };
    static uint64_t nanosecs() {
        using namespace std::chrono;
        auto delta = steady_clock::now() - sys_start_time;
        return duration_cast<nanoseconds>(delta).count();
    }
};
