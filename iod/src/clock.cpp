#include "clock.h"

std::chrono::time_point<std::chrono::steady_clock> Clock::sys_start_time =
    std::chrono::steady_clock::now();
