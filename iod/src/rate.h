#ifndef __rate_limiter_cw__
#define __rate_limiter_cw__

#include <stdint.h>

class RateLimiter {
  public:
    enum BackOffAlgorithm { boaNone, boaStep, boaGeometric };
    RateLimiter();
    RateLimiter(int msec, BackOffAlgorithm boa);

    bool ready();
    void reset() { delay = start_delay; }

    void setStep(uint64_t msecs) { step = msecs * 1000; }
    void setScale(double s) { scale = s; }
    void setMaxDelay(uint64_t maxd_msecs) { max_delay = maxd_msecs * 1000; }

  private:
    RateLimiter &operator=(const RateLimiter &);
    RateLimiter(const RateLimiter &);

    BackOffAlgorithm back_off;
    uint64_t delay;
    uint64_t start_delay;
    uint64_t last;
    uint64_t step;
    uint64_t max_delay;
    double scale;
};

#endif
