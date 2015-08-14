#include <iostream>
#include <map>
#include <string>
#include <sys/time.h>
#include <unistd.h>
#include "watchdog.h"
#include "Logger.h"

std::map<std::string, Watchdog *>Watchdog::all;

Watchdog::Watchdog(const char *wdname, uint64_t timeout, bool autostart, bool once_only ) 
	: name(wdname), last_time(0), time_out(timeout*1000), one_shot(once_only), is_running(autostart) { 
	all[name] = this;
}

void Watchdog::poll(uint64_t t) {
	if (!is_running) {
		is_running = true;
		start(t);
		return;
	}
	if (t==0) t = now();
	if (!one_shot || trigger_time == 0){ last_time = t; }
}
Watchdog::~Watchdog() { all.erase(name); }

void Watchdog::reset() { trigger_time = 0; }
	
bool Watchdog::triggered(uint64_t t) const {
	bool has_triggered = false;
	if (!is_running)
		return false;
	if (!one_shot) has_triggered = last_time<t && t-last_time > time_out;
	else {has_triggered = trigger_time != 0;}
	if (has_triggered)
		{FileLogger fl(program_name); fl.f() << name << " woof: " << (t-last_time)/1000 << "\n"; }
	return has_triggered;
}

uint64_t Watchdog::last() { return last_time; }

uint64_t Watchdog::now() {
	struct timeval now;
	gettimeofday(&now, 0);
	return (uint64_t) now.tv_sec*1000000 + (uint64_t)now.tv_usec;
}

bool Watchdog::running() const { return is_running; }
void Watchdog::stop() { if (!is_running) return; last_time = now(); is_running = false; }
void Watchdog::start(uint64_t t) {
	is_running = true;
	poll(t);
}

bool Watchdog::anyTriggered(uint64_t t) {
	if (t == 0) t = now();
	std::map<std::string, Watchdog*>::iterator iter = all.begin();
	while (iter != all.end()) {
		Watchdog *w = (*iter++).second;
		if (w->triggered(t)) return true;
	}
	return false;
}

bool Watchdog::showTriggered(uint64_t t, bool reset, std::ostream &out) {
	if (t == 0) t = now();
	bool found = false;
	std::map<std::string, Watchdog*>::iterator iter = all.begin();
	while (iter != all.end()) {
		Watchdog *w = (*iter++).second;
		if (w->triggered(t)) {
			found = true;
			out << w->name << " Triggered: " << (t-w->last_time)/1000 << "\n";
			if (reset) w->reset();
		}
	}
	return found;
}

#ifdef TESTING
int main(int argc, char *argv[] ) {
	
	Watchdog wd1("WD1", 100); wd1.start();
	Watchdog wd2("WD2", 100); wd1.start();
	
	wd1.poll(); wd2.poll();
	for(int i=0; i<20; ++i) {
		wd1.poll();
		uint64_t now = Watchdog::now();
		if (wd1.triggered(now)) { std::cout << "wd1 triggered:"<< (now - wd1.last()) <<"\n"; }
		if (wd2.triggered(now)) { std::cout << "wd2 triggered:"<< (now - wd2.last()) <<"\n"; }
		usleep(5000);
		
	}
	
	return EXIT_SUCCESS;
}
#endif
