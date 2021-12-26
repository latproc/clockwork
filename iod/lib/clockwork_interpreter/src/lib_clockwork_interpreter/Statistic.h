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

#ifndef cwlang_statistic_h
#define cwlang_statistic_h

#include "options.h"
#include <lib_clockwork_client/includes.hpp>
#include <limits.h>
#include <list>
#include <ostream>
#include <string>
#include <sys/time.h>
// // #include "cJSON.h"

class Statistic {
  public:
    Statistic(const char *msg)
        : text(msg), sum(0), count(0), min_value(LONG_MAX),
          max_value(LONG_MIN){};
    Statistic &operator=(const Statistic &other);
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const Statistic &other);

    void reset() {
        sum = 0;
        count = 0;
        min_value = LONG_MAX;
        max_value = LONG_MIN;
    }

    void add(long new_value) {
        ++count;
        sum += new_value;
        if (new_value > max_value) {
            max_value = new_value;
        }
        if (new_value < min_value) {
            min_value = new_value;
        }
        ssq += new_value * new_value;
    }
    void report(std::ostream &out) {
        if (count == 0) {
            out << text << "\tNo data\n";
        } else {
            double ave = (count == 0) ? 0 : sum / count;
            out << text << '\t' << count << '\t' << min_value << '\t'
                << max_value << '\t' << ave << "\n";
        }
    }
    void reportArray(cJSON *obj) {
        double ave = (count == 0) ? 0 : sum / count;
        cJSON_AddItemToArray(obj, cJSON_CreateNumber(count));
        cJSON_AddItemToArray(obj, cJSON_CreateNumber(min_value));
        cJSON_AddItemToArray(obj, cJSON_CreateNumber(max_value));
        cJSON_AddItemToArray(obj, cJSON_CreateDouble(ave));
        cJSON_AddItemToArray(obj, cJSON_CreateDouble(sum));
    }
    void report(cJSON *obj) {
        double ave = (count == 0) ? 0 : sum / count;
        cJSON_AddNumberToObject(obj, "count", count);
        cJSON_AddNumberToObject(obj, "min", min_value);
        cJSON_AddNumberToObject(obj, "max", max_value);
        cJSON_AddNumberToObject(obj, "ave", ave);
        cJSON_AddNumberToObject(obj, "sum", sum);
    }
#if 0
    double rollingAverage() {
        int c = periods.size();
        if (c == 0) {
            return 0.0f;
        }
        double s = 0.0f;
        for (int i = 0; i < c; i++) {
            s += average(i);
        }
        return s / c;
    }
#endif
    static void add(Statistic *new_stat) { stats.push_back(new_stat); }
    static void reportAll(std::ostream &out) {
        std::list<Statistic *>::iterator iter = stats.begin();
        while (iter != stats.end()) {
            Statistic *stat = *iter++;
            stat->report(out);
        }
    }
    static void reportAll(cJSON *obj) {
        std::list<Statistic *>::iterator iter = stats.begin();
        while (iter != stats.end()) {
            cJSON *item = cJSON_CreateObject();
            cJSON *info = cJSON_CreateArray();
            Statistic *stat = *iter++;
            stat->reportArray(info);
            cJSON_AddItemToObject(item, stat->getName().c_str(), info);
            cJSON_AddItemToArray(obj, item);
        }
    }
    const std::string &getName() const { return text; }
    int getCount() { return count; }
    double getSum() { return sum; }

  private:
    Statistic(const Statistic &orig);

    std::string text;
    double sum;
    int count;
    long min_value;
    long max_value;
    double ssq;
    static std::list<Statistic *> stats;
};

std::ostream &operator<<(std::ostream &out, const Statistic &m);

class CaptureDuration {
  public:
    CaptureDuration(Statistic &stat) : statistic(stat) {
        if (keep_statistics()) {
            start = microsecs();
        } else {
            start = 0;
        }
    }

    ~CaptureDuration() {
        if (keep_statistics()) {
            struct timeval now;
            gettimeofday(&now, 0);
            uint64_t duration = microsecs() - start;
            statistic.add(duration);
        }
    };

  private:
    Statistic &statistic;
    uint64_t start;
};

#

#endif
