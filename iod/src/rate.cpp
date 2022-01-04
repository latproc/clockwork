#include "rate.h"
#include <iostream>
#include <sys/time.h>

static uint64_t microsecs() {
    struct timeval now;
    gettimeofday(&now, 0);
    return (uint64_t)now.tv_sec * 1000000L + (uint64_t)now.tv_usec;
}

RateLimiter::RateLimiter()
    : back_off(boaGeometric), delay(100000), start_delay(100000), last(0), step(20000),
      max_delay(4000000), scale(1.05) {}

RateLimiter::RateLimiter(int msec, BackOffAlgorithm boa)
    : back_off(boa), delay(msec * 1000), start_delay(msec * 1000), last(0), step(20000),
      max_delay(4000000), scale(1.0) {}

bool RateLimiter::ready() {
    uint64_t now = microsecs();
    if (now - last >= delay) {
        last = now;
        if (back_off == boaStep) {
            delay += step;
        }
        else if (back_off == boaGeometric) {
            delay *= scale;
        }
        if (delay > max_delay) {
            delay = max_delay;
        }
        return true;
    }
    return false;
}

#ifdef TESTING

#include "boost/date_time/posix_time/posix_time.hpp"
void Test(RateLimiter &rl) {
    if (!rl.ready()) {
        return;
    }
    boost::posix_time::ptime t(boost::posix_time::microsec_clock::local_time());
    std::cout << t << ": Test\n";
}

int main(int argc, char *argv[]) {
    RateLimiter rl(150, RateLimiter::boaStep);

    for (;;) {
        Test(rl);
        usleep(1);
    }
}
#endif
