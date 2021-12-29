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
#include "boost/thread/mutex.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string>
#include <sys/time.h>

LogState *LogState::state_instance = 0;
static std::string header;
Logger *Logger::logger_instance = 0;
struct timeval last_time;

int Logger::None;
int Logger::Debug;
int Logger::Important;
int Logger::Everything;

Logger::Logger() : log_level(None), dummy_output(0), log_stream(&std::cout) {
    last_time.tv_sec = 0;
    last_time.tv_usec = 0;
    None = LogState::instance()->insert(LogState::instance()->define("None"));
    Important = LogState::instance()->insert(LogState::instance()->define("Important"));
    Debug = LogState::instance()->insert(LogState::instance()->define("Debug"));
    Everything = LogState::instance()->insert(LogState::instance()->define("Everything"));
}

class Internals {
  public:
    bool allocated;
    std::ostream *f;
    std::ofstream &file() {
        if (!f) {
            f = new std::ofstream;
            allocated = true;
        }
        return *(std::ofstream *)f;
    }
    Internals() : allocated(false), f(0) {}
    ~Internals() { /**f << std::flush;*/
        if (allocated)
            delete f;
    }
};

FileLogger::FileLogger(const char *fname) : internals(0), lock(mutex_) {
    internals = new Internals;
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
    delete internals;
    lock.unlock();
}

void FileLogger::getTimeString(char *buf, size_t buf_size) {
    struct timeval now_tv;
    gettimeofday(&now_tv, 0);
    struct tm now_tm;
    localtime_r(&now_tv.tv_sec, &now_tm);
    uint32_t msec = now_tv.tv_usec;
    snprintf(buf, 50, "%04d-%02d-%02d %02d:%02d:%02d.%06d ", now_tm.tm_year + 1900,
             now_tm.tm_mon + 1, now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec, msec);
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
    struct timeval now_tv;
    gettimeofday(&now_tv, 0);
    struct tm now_tm;
    localtime_r(&now_tv.tv_sec, &now_tm);
    uint32_t msec = now_tv.tv_usec;
    snprintf(buf, 50, "%04d-%02d-%02d %02d:%02d:%02d.%06d ", now_tm.tm_year + 1900,
             now_tm.tm_mon + 1, now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec, msec);
}
bool LogState::includes(int flag_num) { return state_flags.count(flag_num); }

std::ostream &Logger::log(Level l) {
    boost::mutex::scoped_lock lock(mutex_);
    if (!dummy_output) {
        dummy_output = new std::stringstream;
    }

    if (LogState::instance()->includes(l)) {
        char buf[50];
        getTimeString(buf, 50);
        *log_stream << buf;
        return *log_stream;
    }
    else {
        dummy_output->clear();
        dummy_output->seekp(0);
        return *dummy_output;
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
