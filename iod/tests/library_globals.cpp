#include <Statistic.h>
#include <Statistics.h>
#include <StatisticList.h>
#include <value.h>

// TODO: the library still needs some globals...
bool program_done = false;
bool machine_is_ready = false;

Statistics *statistics = NULL;
StatisticList<long> *all_statistics = NULL;
