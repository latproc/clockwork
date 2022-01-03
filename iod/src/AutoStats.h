#ifndef __AUTO_STATS_H__
#define __AUTO_STATS_H__

#include <stdint.h>

class AutoStatStorage {
public:
	// auto stats are linked to system properties
	AutoStatStorage(const char *time_name, const char *rate_name, unsigned int reset_every = 100);
	const long *setupPropertyRef(const char *machine_name, const char *property_name);
	void reset();
	void update(uint64_t now, uint64_t duration);
	bool running();
	void start();
	void stop();
	void update();
protected:
	uint64_t start_time;
	uint64_t last_update;
	uint32_t total_polls;
	uint64_t total_time;
	uint64_t total_delays;
	const long *rate_property;
	const long *time_property;
	unsigned int reset_point;
};

class AutoStat {
public:
	AutoStat(AutoStatStorage &storage);
	~AutoStat();

private:
	uint64_t start_time;
	AutoStatStorage &storage_;
};

#endif
