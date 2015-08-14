#ifndef __WATCHDOG__H__
#define __WATCHDOG__H__

#include <iostream>
#include <map>
#include <string>

class Watchdog {
public:
	Watchdog(const char *wdname, uint64_t timeout, bool autostart = true, bool once_only = false );
	~Watchdog();
	void poll(uint64_t now = 0);
	void reset();
	
	bool triggered(uint64_t t) const;
	uint64_t last();

	bool running()const;
	void stop();
	void start(uint64_t = 0);

	static uint64_t now();
	static std::map<std::string, Watchdog *>all;
	static bool anyTriggered(uint64_t now = 0);
	static bool showTriggered(uint64_t now = 0, bool reset = false, std::ostream &out = std::cerr) ;

private:
	std::string name;
	uint64_t last_time;
	uint64_t time_out;
	uint64_t trigger_time;
	bool one_shot;
	bool is_running;
	
	Watchdog(const Watchdog &other);
	Watchdog & operator=(const Watchdog &other);
	
};

#endif
