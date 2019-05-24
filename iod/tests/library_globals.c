#include <value.h>
#include <Statistics.h>
#include <Statistic.h>

// TODO: the library still needs some globals...
bool program_done = false;
bool machine_is_ready = false;
bool machine_was_ready = false;


Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;
