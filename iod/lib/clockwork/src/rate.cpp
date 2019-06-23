#include <iostream>
#include "Win32Helper.h"
//#include <sys/time.h>
#include "rate.h"
#include <ctime>
#include <chrono>

RateLimiter::RateLimiter() : back_off(boaGeometric),
  delay(std::chrono::microseconds(100000)),
  start_delay(std::chrono::microseconds(100000)),
  last(std::chrono::system_clock::now()),
  step(std::chrono::microseconds(20000)),
  max_delay(std::chrono::microseconds(4000000)),
  scale(1.05)  {
}

RateLimiter::RateLimiter(int msec, BackOffAlgorithm boa) : back_off(boa), delay(std::chrono::microseconds(msec*1000)),
    start_delay(std::chrono::microseconds(msec*1000)),
	last(std::chrono::system_clock::now()),
    step(std::chrono::microseconds(20000)),
    max_delay(std::chrono::microseconds(4000000)),
    scale(1.0)  {
}

void RateLimiter::setStep(uint64_t msecs) { step = std::chrono::microseconds(msecs * 1000); }
void RateLimiter::setScale(double s) { scale = s; }
void RateLimiter::setMaxDelay( uint64_t maxd_msecs) { max_delay = std::chrono::microseconds(maxd_msecs*1000); }


bool RateLimiter::ready() {
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	//std::chrono::microseconds delta_t = std::chrono::duration_cast<std::chrono::microseconds>(now - last);
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
