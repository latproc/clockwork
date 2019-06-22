#ifndef __rate_limiter_cw__
#define __rate_limiter_cw__

#include <chrono>

class RateLimiter {
public:
	enum BackOffAlgorithm { boaNone, boaStep, boaGeometric };
	RateLimiter();
	RateLimiter(int msec, BackOffAlgorithm boa);

	bool ready();
	void reset() { delay = start_delay; }

	void setStep(uint64_t msecs);
	void setScale(double s);
	void setMaxDelay( uint64_t maxd_msecs);

private:
	RateLimiter &operator=(const RateLimiter &);
	RateLimiter(const RateLimiter&);

	BackOffAlgorithm back_off;
	std::chrono::microseconds delay;
	std::chrono::microseconds start_delay;
	std::chrono::system_clock::time_point last;
	std::chrono::microseconds step;
	std::chrono::microseconds max_delay;
	double scale;
};

#endif
