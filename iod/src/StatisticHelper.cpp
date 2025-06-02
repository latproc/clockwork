#include "StatisticHelper.h"
#include "cJSON.h"

StatisticList<long> *all_statistics = nullptr;

Statistic &addStatistic(const char *msg) {
    if (all_statistics == nullptr) {
        all_statistics = new StatisticList<long>();
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

cJSON *reportStatistic(const Statistic &stat, cJSON *obj) {
        if (obj == nullptr) {
                obj = cJSON_CreateObject();
        }
        stat.report(obj);
        return obj;
}
