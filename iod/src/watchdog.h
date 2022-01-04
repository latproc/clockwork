#ifndef __WATCHDOG__H__
#define __WATCHDOG__H__

#include "boost/thread/mutex.hpp"
#include <iostream>
#include <map>
#include <string>

class Watchdog {
  public:
    Watchdog(const char *wdname, int64_t timeout, bool autostart = true, bool once_only = false);
    ~Watchdog();
    void poll(int64_t now = 0);
    void poll(uint64_t now);
    void reset();

    bool triggered(int64_t t);
    bool triggered(uint64_t t);
    int64_t last();

    bool running() const;
    void stop();
    void start(int64_t = 0);
    void start(uint64_t);

    static int64_t now();
    static std::map<std::string, Watchdog *> all;
    static bool anyTriggered(int64_t now = 0);
    static bool anyTriggered(uint64_t now);
    static bool showTriggered(int64_t now = 0, bool reset = false, std::ostream &out = std::cerr);
    static bool showTriggered(uint64_t now, bool reset = false, std::ostream &out = std::cerr);

  private:
    boost::mutex mutex;
    std::string name;
    int64_t last_time;
    int64_t time_out;
    int64_t trigger_time;
    bool one_shot;
    bool is_running;

    Watchdog(const Watchdog &other);
    Watchdog &operator=(const Watchdog &other);
};

#endif
