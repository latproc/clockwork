#ifndef __rate_limiter_cw__
#define __rate_limiter_cw__

namespace cw {

class TimeSource {
  public:
	enum Units { Microseconds, Milliseconds, Seconds, Minutes, Hours, Days, Weeks, Years };
	virtual uint64_t now() const = 0;
	virtual Units units() const = 0;
	virtual uint64_t to_microsecs() const = 0;
	virtual ~TimeSource() {}
};

class MicrosecTimeSource : public TimeSource {
  public:
	virtual uint64_t now() const;
	virtual Units units() const { return Microseconds; }
	virtual uint64_t to_microsecs() const { return 1; }
	virtual ~MicrosecTimeSource();
};

class MillisecTimeSource : public TimeSource {
  public:
	virtual uint64_t now() const;
	virtual Units units() const { return Milliseconds; }
	virtual uint64_t to_microsecs() const { return 1000; }
	virtual ~MillisecTimeSource();
};

extern MicrosecTimeSource default_microsecond_source;
extern MillisecTimeSource default_millisecond_source;

};

class RateLimiter {
public:
	enum BackOffAlgorithm { boaNone, boaStep, boaGeometric };
	RateLimiter();
	RateLimiter(int msec, BackOffAlgorithm boa, const cw::TimeSource &ts = cw::default_microsecond_source);

	bool ready();
	void reset() { delay = start_delay; }
	void enable(bool which) { _enabled = which; }
	bool enabled() const { return _enabled; }

	void setStep(uint64_t msecs) {
		step = msecs * time_source.to_microsecs();
	}
	void setScale(double s) { scale = s; }
	void setMaxDelay( uint64_t maxd_msecs) { 
		max_delay = maxd_msecs * time_source.to_microsecs(); 
	}

private:
	RateLimiter &operator=(const RateLimiter &);
	RateLimiter(const RateLimiter&);

	const cw::TimeSource &time_source;
	bool _enabled;
	BackOffAlgorithm back_off;
	uint64_t delay;
	uint64_t start_delay;
	uint64_t last;
	uint64_t step;
	uint64_t max_delay;
	double scale;
};

#endif

