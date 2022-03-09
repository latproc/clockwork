#include "watchdog.h"
#include "Logger.h"
#include "boost/thread/mutex.hpp"
#include "value.h"
#include <iostream>
#include <map>
#include <string>
#include <sys/time.h>
#include <unistd.h>

std::map<std::string, Watchdog *> Watchdog::all;

Watchdog::Watchdog(const char *wdname, int64_t timeout, bool autostart, bool once_only)
    : name(wdname), last_time(0), time_out(timeout * 1000), one_shot(once_only),
      is_running(autostart) {
    all[name] = this;
}

void Watchdog::poll(int64_t t) {
    //std::cout <<  name << " poll " << ( (is_running) ? "running" : "not running") << " " << last_time << "\n";
    if (!is_running) {
        is_running = true;
        start(t);
        return;
    }
    if (t == 0) {
        t = now();
    }
    if (!one_shot || trigger_time == 0) {
        last_time = t;
        //NB_MSG <<name << " last_time is now: " << t << "\n";
    }
}
void Watchdog::poll(uint64_t t) { poll((int64_t)t); }
Watchdog::~Watchdog() { all.erase(name); }

void Watchdog::reset() {
    boost::mutex::scoped_lock lock(mutex);
    //std::cout <<  name << " reset " << ( (is_running) ? "running" : "not running") << "\n";
    trigger_time = 0;
}

bool Watchdog::triggered(int64_t t) {
    boost::mutex::scoped_lock lock(mutex);
    if (t == 0) {
        t = now();
    }
    if (!is_running) {
        //NB_MSG <<name << " Watchdog is disabled\n";
        return false;
    }
    bool has_triggered = false;
    if (!one_shot) {
        has_triggered = last_time < t && t - last_time > time_out;
    }
    else {
        has_triggered = trigger_time != 0;
    }
    if (has_triggered) {
        //std::cout << name << " woof: " << (t-last_time)/1000 << "\n";
        {
            FileLogger fl(program_name);
            fl.f() << name << " woof: " << (int64_t)(t - last_time) / 1000 << "\n";
        }
    }
    return has_triggered;
}

bool Watchdog::triggered(uint64_t t) {
    boost::mutex::scoped_lock lock(mutex);
    return triggered((int64_t)t);
}

int64_t Watchdog::last() { return last_time; }

static boost::mutex time_mutex;
int64_t Watchdog::now() {
    boost::mutex::scoped_lock lock(time_mutex);
    return microsecs();
}

bool Watchdog::running() const { return is_running; }
void Watchdog::stop() {
    //std::cout <<  name << " stop " << ( (is_running) ? "running" : "not running") << "\n";
    last_time = now();
    is_running = false;
}
void Watchdog::start(int64_t t) {
    //std::cout <<  name << " start " << ( (is_running) ? "running" : "not running") << "\n";
    is_running = true;
    poll(t);
}

void Watchdog::start(uint64_t t) { poll((int64_t)t); }

bool Watchdog::anyTriggered(int64_t t) {
    if (t == 0) {
        t = now();
    }
    std::map<std::string, Watchdog *>::iterator iter = all.begin();
    while (iter != all.end()) {
        Watchdog *w = (*iter++).second;
        if (w->triggered(t)) {
            return true;
        }
    }
    return false;
}
bool Watchdog::anyTriggered(uint64_t t) { return anyTriggered((int64_t)t); }

bool Watchdog::showTriggered(int64_t t, bool reset, std::ostream &out) {
    if (t == 0) {
        t = now();
    }
    bool found = false;
    std::map<std::string, Watchdog *>::iterator iter = all.begin();
    while (iter != all.end()) {
        Watchdog *w = (*iter++).second;
        if (w->triggered(t)) {
            found = true;
            out << w->name << " Triggered: " << (int64_t)(t - w->last_time) / 1000 << "\n";
            if (reset) {
                w->reset();
            }
        }
    }
    return found;
}
bool Watchdog::showTriggered(uint64_t t, bool reset, std::ostream &out) {
    return showTriggered((int64_t)t, reset, out);
}

#ifdef TESTING
int main(int argc, char *argv[]) {

    Watchdog wd1("WD1", 100);
    wd1.start();
    Watchdog wd2("WD2", 100);
    wd1.start();

    wd1.poll();
    wd2.poll();
    for (int i = 0; i < 20; ++i) {
        wd1.poll();
        uint64_t now = Watchdog::now();
        if (wd1.triggered(now)) {
            std::cout << "wd1 triggered:" << (now - wd1.last()) << "\n";
            wd1.reset();
        }
        if (wd2.triggered(now)) {
            std::cout << "wd2 triggered:" << (now - wd2.last()) << "\n";
            wd2.reset();
        }
        usleep(5000);
    }

    return EXIT_SUCCESS;
}
#endif
