#ifndef __CounterRateFilterSettings_H__
#define __CounterRateFilterSettings_H__

#include "filtering.h"

class CounterRateFilterSettings {
public:
  long position;
  long velocity;
  bool property_changed;
  // analogue filter fields
  uint32_t noise_tolerance; // filter out changes with +/- this range
  int32_t last_sent; // this is the value to send unless the read value moves away from the mean
  int32_t last_pos;

  uint64_t start_t;
  uint64_t update_t;
  uint64_t last_update_t;
  SampleBuffer readings;
  long zero_count;
  CounterRateFilterSettings(unsigned int sz) : position(0), velocity(0), property_changed(false),
    noise_tolerance(20),last_sent(0), last_pos(0), start_t(0), update_t(0), last_update_t(0),
    readings(sz), zero_count(0) {
	struct timeval now;
	gettimeofday(&now, 0);
	update_t = start_t;
}
};

#endif
