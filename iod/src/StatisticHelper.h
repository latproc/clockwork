#pragma once

#include "Statistic.h"
#include "StatisticList.h"

extern StatisticList<long> *all_statistics;

template <typename T>
UntypedStatistic<T> &addStatistic(const char *msg) {
    if (all_statistics == nullptr) {
        all_statistics = new StatisticList<T>();
    }
#if 0
    for (const auto &stat : *all_statistics) {
        if (stat->getName() == msg) {
            return *stat;
        }
    }
#endif
    all_statistics->emplace_back(msg);
    return all_statistics->back();
}

Statistic &addStatistic(Statistic &stat);
cJSON *reportStatistic(const Statistic &stat, cJSON *obj);

template <typename T>
struct RegisteredStatistic {
    RegisteredStatistic(const char *msg) : stat(addStatistic<T>(msg)) {}

    RegisteredStatistic(const RegisteredStatistic &) = delete;
    RegisteredStatistic &operator=(const RegisteredStatistic &) = delete;

    ~RegisteredStatistic() = default;

    void reset() { stat.reset(); }
    void add(T new_value) { stat.add(new_value); }
    void report(std::ostream &out) const { stat.report(out); }
    void report(cJSON *obj) const { stat.report(obj); }
    bool operator==(const RegisteredStatistic &other) const { return stat == other.stat; }
    std::ostream &operator<<(std::ostream &out) const { return stat.operator<<(out); }
    const std::string &getName() const { return stat.getName(); }
    int getCount() const { return stat.getCount(); }
    double getSum() const { return stat.getSum(); }
    double mean() const { return stat.mean(); }
    T getMin() const { return stat.getMin(); }
    T getMax() const { return stat.getMax(); }

    UntypedStatistic<T> &stat;
};
