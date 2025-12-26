/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc

    Latproc is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    Latproc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Latproc; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "Logger.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/time.h>
#include <mutex>

#include "MessageLog.h"

class Logger::Internals {
  public:
    static Logger *logger_instance;
    Level log_level;
    std::stringstream dummy_output;
    std::ostream *log_stream = nullptr;

    Internals() : log_level(None), dummy_output(0), log_stream(&std::cout) {}
};

// Mutex ordering constraint:
// - logger_mutex protects Logger internals
// - logstate_mutex protects LogState internals
// - When both must be acquired, always acquire logger_mutex BEFORE logstate_mutex.
// - Do NOT call Logger APIs from within LogState while holding logstate_mutex to avoid ABBA deadlocks.
static std::mutex logger_mutex;
static std::mutex logstate_mutex;

Logger::Internals *Logger::internals = nullptr;
LogState *LogState::state_instance = nullptr;
Logger *Logger::Internals::logger_instance = nullptr;
timeval last_time;

LogState::LogState() {
    std::cout << "creating LogState\n";
    flag_names.reserve(50);
}

LogState *LogState::instance() {
    if (!state_instance) {
        state_instance = new LogState;
    }
    return state_instance;
}

void LogState::cleanup() {
    std::lock_guard<std::mutex> lock(logstate_mutex);
    delete state_instance;
    state_instance = nullptr;
}

int LogState::define(const std::string & new_name) {
    flag_names.push_back(new_name);
    int logid = name_map[new_name] = static_cast<int>(flag_names.size()-1);
    return logid;
}
int LogState::lookup(const std::string &name) {
    std::lock_guard<std::mutex> lock(logstate_mutex);
    NameMapIterator iter = name_map.find(name);
    if (iter != name_map.end()) {
        return (*iter).second;
    }
    else {
        return 0;
    }
}
int LogState::insert(int flag_num) {
    std::lock_guard<std::mutex> lock(logstate_mutex);
    state_flags.insert(flag_num);
    return flag_num;
}
int LogState::insert(std::string name) {
    std::lock_guard<std::mutex> lock(logstate_mutex);

    NameMapIterator iter = name_map.find(name);
    if (iter != name_map.end()) {
        int n = (*iter).second;
        state_flags.insert(n);
        return n;
    }
    return -1;
}
void LogState::erase(int flag_num) {
    std::lock_guard<std::mutex> lock(logstate_mutex);
    state_flags.erase(flag_num);
}
void LogState::erase(std::string name){
    std::lock_guard<std::mutex> lock(logstate_mutex);
    NameMapIterator iter = name_map.find(name);
    if (iter != name_map.end()) {
        state_flags.insert((*iter).second);
    }
}
bool LogState::includes(std::string name) {
    std::lock_guard<std::mutex> lock(logstate_mutex);
    return name_map.find(name) != name_map.end();
}

int Logger::None = 0;
int Logger::Debug = 0;
int Logger::Important = 0;
int Logger::Everything = 0;

Logger::Logger() {
    last_time.tv_sec = 0;
    last_time.tv_usec = 0;
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(logger_mutex);
    delete internals;
    internals = nullptr;
}

Logger *Logger::instance() {
    // Note: We hold logger_mutex here; subsequent calls into LogState must not acquire logger_mutex and should use logstate_mutex only.
    std::lock_guard<std::mutex> lock(logger_mutex);
    if (!internals) {
        internals = new Internals();
    }
    if (!internals->logger_instance) {
        internals->logger_instance = new Logger;
        auto logstate = LogState::instance();
        None = logstate->insert(logstate->define("None"));
        Important = logstate->insert(logstate->define("Important"));
        Debug = logstate->insert(logstate->define("Debug"));
        Everything = logstate->insert(logstate->define("Everything"));
    }
    return internals->logger_instance;
}
Logger::Level Logger::level() {
    std::lock_guard<std::mutex> lock(logger_mutex);
    if (!internals) { return None; }
    return internals->log_level;
}

void Logger::setLevel(Level n) {
    std::lock_guard<std::mutex> lock(logger_mutex);
    if (!internals) { (void)Logger::instance(); }
    internals->log_level = n;
}

void Logger::setOutputStream(std::ostream *out) {
    std::lock_guard<std::mutex> lock(logger_mutex);
    if (!internals) { (void)Logger::instance(); }
    internals->log_stream = out;
}

void Logger::cleanup() {
    std::lock_guard<std::mutex> lock(logger_mutex);
    if (internals) {
        delete internals->logger_instance;
        internals->logger_instance = nullptr;
        delete internals;
        internals = nullptr;
    }
}

class FileLogger::Internals {
  public:
    std::mutex m_mutex;
    std::unique_lock<std::mutex> lock;
    bool allocated;
    std::ostream *f;
    std::ofstream &file() {
        if (!f) {
            f = new std::ofstream;
            allocated = true;
        }
        return *(std::ofstream *)f;
    }
    Internals() : lock(m_mutex), allocated(false), f(0) {
        lock.unlock();
    }
    ~Internals() { /**f << std::flush;*/
        if (allocated) {
            delete f;
        }
    }
};

FileLogger::FileLogger(const char *fname) : internals(0) {
    internals = new Internals;
    internals->lock.lock();
    char buf[40];
    getTimeString(buf, 40);
#if 0
    std::string n("/tmp/");
    n += fname;
    n + ".txt";
    internals->file().open(n,  std::ofstream::out | std::ofstream::app);
    internals->file() << program_name << " " << buf << " ";
#else
    internals->f = &std::cerr;
    *internals->f << buf << " ";
#endif
}

std::ostream &FileLogger::f() {
    if (!internals->f) {
        internals->f = new std::ofstream;
        internals->allocated = true;
    }
    return internals->file();
}

FileLogger::~FileLogger() {
    internals->lock.unlock();
    delete internals;
}

void FileLogger::getTimeString(char *buf, size_t buf_size) {
    if (buf_size >= 28) {
        struct timeval now_tv;
        gettimeofday(&now_tv, 0);
        struct tm now_tm;
        localtime_r(&now_tv.tv_sec, &now_tm);
        uint32_t msec = now_tv.tv_usec;
        snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d.%06d ", now_tm.tm_year + 1900,
                 now_tm.tm_mon + 1, now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec,
                 msec);
    }
}

void Logger::setLevel(std::string level_name) {
    if (level_name == "None") {
        setLevel(None);
    }
    else if (level_name == "Important") {
        setLevel(Important);
    }
    else if (level_name == "Debug") {
        setLevel(Debug);
    }
    else if (level_name == "Everything") {
        setLevel(Everything);
    }
}

void Logger::getTimeString(char *buf, size_t buf_size) {
    if (buf_size >= 28) {
        struct timeval now_tv;
        gettimeofday(&now_tv, 0);
        struct tm now_tm;
        localtime_r(&now_tv.tv_sec, &now_tm);
        uint32_t msec = now_tv.tv_usec;
        snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d.%06d ", now_tm.tm_year + 1900,
                 now_tm.tm_mon + 1, now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec,
                 msec);
    }
}
bool LogState::includes(int flag_num) { return state_flags.count(flag_num); }

std::ostream &Logger::log(Level l) {
    std::lock_guard<std::mutex> lock(logger_mutex);
    if (!internals) { (void)Logger::instance(); }

    if (LogState::instance()->includes(l)) {
        char buf[40];
        getTimeString(buf, 40);
        *internals->log_stream << buf;
        return *internals->log_stream;
    } else {
        internals->dummy_output.clear();
        internals->dummy_output.seekp(0);
        return internals->dummy_output;
    }
}
std::ostream &LogState::operator<<(std::ostream &out) const {
    std::map<std::string, int>::const_iterator iter = name_map.begin();
    const char *delim = "";
    while (iter != name_map.end()) {
        std::pair<std::string, int> const curr = *iter++;
        out << delim << curr.first << " " << (state_flags.count(curr.second) ? "on" : "off");
        delim = "\n";
    }
    //out << std::flush;
    return out;
}

std::ostream &operator<<(std::ostream &out, const LogState &ls) { return ls.operator<<(out); }

#ifdef LOGGER_DEBUG
int main(int argc, char **argv) {
    Logger::setLevel(Logger::Debug);

    std::cout << " Logger settings:\n" << LogState::instance() << "\n";
    DBG_MSG << "this is a Debug log test\n";
    NB_MSG << "this is an Important log test\n";
    INF_MSG << "this is not very Important\n";

    return 0;
}
#endif

