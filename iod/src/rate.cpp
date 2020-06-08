#include <iostream>
#include <sys/time.h>
#include "rate.h"

static uint64_t microsecs() {
    struct timeval now;
    gettimeofday(&now, 0);
    return (uint64_t)now.tv_sec * 1000000L + now.tv_usec;
}

namespace cw {

	MicrosecTimeSource default_microsecond_source;
	MillisecTimeSource default_millisecond_source;

	uint64_t MicrosecTimeSource::now() const {
		return microsecs();
	}

	MicrosecTimeSource::~MicrosecTimeSource() {}

	uint64_t MillisecTimeSource::now() const {
		return microsecs() / 1000;
	}

	MillisecTimeSource::~MillisecTimeSource() {}

};

RateLimiter::RateLimiter() 
  : time_source(cw::default_microsecond_source), _enabled(true),
	back_off(boaGeometric), delay(100000), start_delay(100000),
	last(0), step(20000), max_delay(4000000), scale(1.05)  {}

RateLimiter::RateLimiter(int msec, BackOffAlgorithm boa, const cw::TimeSource &ts) 
  : time_source(ts), _enabled(true), 
	back_off(boa), delay(msec*1000), start_delay(msec*1000),
	last(0), step(20000), max_delay(4000000)  {}

bool RateLimiter::ready() {
	if (!_enabled) return true;
	uint64_t now = time_source.now() * time_source.to_microsecs();
	if (now >= last + delay) {
		last = now; 
		if (back_off == boaStep)
			delay += step;
		else if (back_off == boaGeometric)
			delay *= scale;
		if (delay> max_delay)
			delay = max_delay;
		return true;
	}
	return false;
}

#ifdef TESTING

#include "boost/date_time/posix_time/posix_time.hpp"
void Test(RateLimiter &rl) {
	if (!rl.ready()) return;
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
