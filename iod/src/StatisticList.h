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
#include "Statistic.h"
#include <cassert>
#include <list>

template <typename T> class StatisticList {
    public:

    void emplace_back(const char *msg) {
        stats.push_back(new UntypedStatistic<T>(msg));
    }

    UntypedStatistic<T> &back() {
        if (stats.empty()) {
            throw std::runtime_error("No statistics available");
        }
        return *stats.back();
    }

    void resetAll() {
        for (auto &stat : stats) {
            stat->reset();
        }
    }
    void add(UntypedStatistic<T> *new_stat) { stats.push_back(new_stat); }
    void reportAll(std::ostream &out) {
            for (auto &stat : stats) {
            stat->report(out);
            }
    }
    void reportAll(cJSON *obj) {
        assert(obj != nullptr);
        assert(obj->type == cJSON_Array);
        for (auto &stat : stats) {
            cJSON *item = cJSON_CreateObject();
            stat->report(item);
            cJSON_AddItemToArray(obj, item);
        }
    }

    typename std::list<UntypedStatistic<T> *>::const_iterator begin() const {
        return stats.begin();
    }
    typename std::list<UntypedStatistic<T> *>::const_iterator end() const {
            return stats.end();
    }
private:
   std::list<UntypedStatistic<T> *> stats;
};
