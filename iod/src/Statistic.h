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

#pragma once

#include "cJSON.h"
#include "options.h"
#include <limits>
#include <string>
#include <ostream>
#include <sys/time.h>
#include <stdexcept>

uint64_t microsecs();

template <typename T>
class UntypedStatistic {
  public:
    UntypedStatistic(const char *msg)
        : text(msg), sum(0), count(0),
          min_value(std::numeric_limits<T>::max()),
          max_value(std::numeric_limits<T>::min()), ssq(0) {}
    UntypedStatistic &operator=(const UntypedStatistic &other);
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const UntypedStatistic &other);

    void reset() {
        sum = 0;
        count = 0;
        min_value = std::numeric_limits<T>::max();
        max_value = std::numeric_limits<T>::min();
    }

    void add(T new_value) {
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

    void report(std::ostream &out) const {
        if (count == 0) {
            out << text << "\tNo data\n";
        }
        else {
            double ave = (count == 0) ? 0 : sum / count;
            out << text << '\t' << count << '\t' << min_value << '\t' << max_value << '\t' << ave
                << "\n";
        }
    }

    void report(cJSON *obj) const {
        double ave = (count == 0) ? 0 : sum / count;
        if (obj->type == cJSON_Array) {
            cJSON_AddItemToArray(obj, cJSON_CreateNumber(count));
            cJSON_AddItemToArray(obj, cJSON_CreateNumber(min_value));
            cJSON_AddItemToArray(obj, cJSON_CreateNumber(max_value));
            cJSON_AddItemToArray(obj, cJSON_CreateDouble(ave));
            cJSON_AddItemToArray(obj, cJSON_CreateDouble(sum));
        } else if (obj->type == cJSON_Object) {
            cJSON_AddStringToObject(obj, "name", text.c_str());
            cJSON_AddNumberToObject(obj, "count", count);
            cJSON_AddNumberToObject(obj, "min", min_value);
            cJSON_AddNumberToObject(obj, "max", max_value);
            cJSON_AddNumberToObject(obj, "ave", ave);
            cJSON_AddNumberToObject(obj, "sum", sum);
        }
        else {
            throw std::runtime_error("Invalid cJSON object type for reporting statistics");
        }
    }

    const std::string &getName() const { return text; }
    int getCount() const { return count; }
    double getSum() const { return sum; }
    double mean() const { return (count != 0) ? sum / count : 0; }
    T getMin() const { return min_value; }
    T getMax() const { return max_value; }

  private:
    UntypedStatistic(const UntypedStatistic &orig);

    std::string text;
    double sum;
    int count;
    T min_value;
    T max_value;
    double ssq;
};

template <typename T>
std::ostream &operator<<(std::ostream &out, const UntypedStatistic<T> &m);

template <typename T>
class CaptureDuration {
  public:
    CaptureDuration(UntypedStatistic<T> &stat) : statistic(stat) {
        if (keep_statistics()) {
            start = microsecs();
        }
        else {
            start = 0;
        }
    }

    ~CaptureDuration() {
        if (keep_statistics()) {
            uint64_t duration = microsecs() - start;
            statistic.add(duration);
        }
    };

  private:
    UntypedStatistic<T> &statistic;
    uint64_t start;
};

using Statistic = UntypedStatistic<long>;

