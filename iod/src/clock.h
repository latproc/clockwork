#pragma once

#include <chrono>

class Clock {
    static std::chrono::time_point<std::chrono::steady_clock> sys_start_time;

  public:
    static uint64_t clock() {
        auto delta = std::chrono::steady_clock::now() - sys_start_time;
        return std::chrono::duration_cast<std::chrono::microseconds>(delta).count();
    };
};
