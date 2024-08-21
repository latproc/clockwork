#pragma once

#include <thread>

struct ClockSync {
#ifdef USE_CHRONO
    std::thread timer_thread;
#endif
    ClockSync();
    ~ClockSync();
    void operator()();
};

// block until the next clock tick

