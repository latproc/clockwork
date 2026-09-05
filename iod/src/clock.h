#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

class Clock {
    static std::chrono::time_point<std::chrono::steady_clock> sys_start_time;
    // Test-only monotonic clock override (µs). 0 means use the real steady clock.
    static std::atomic<uint64_t> test_now_us;

  public:
    static uint64_t clock() {
        uint64_t t = test_now_us.load(std::memory_order_relaxed);
        if (t != 0) {
            return t;
        }
        auto delta = std::chrono::steady_clock::now() - sys_start_time;
        return std::chrono::duration_cast<std::chrono::microseconds>(delta).count();
    };
    // Test-only: pin the monotonic clock to an absolute µs value (and enable the
    // override). Call clearTestTime() to return to the real steady clock.
    static void setTestTime(uint64_t us) { test_now_us.store(us, std::memory_order_relaxed); }
    static void advanceTestTime(uint64_t us) {
        test_now_us.fetch_add(us, std::memory_order_relaxed);
    }
    static void clearTestTime() { test_now_us.store(0, std::memory_order_relaxed); }
};
