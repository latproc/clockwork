//#include <value.h>
//#include <Statistics.h>
//#include <Statistic.h>
//#include <MachineClass.h>
//#include <MachineInterface.h>

// TODO: the library still needs some globals...
bool program_done = false;
bool machine_is_ready = false;
bool machine_was_ready = false;

std::list<MachineClass*> MachineClass::all_machine_classes;
std::map<std::string, MachineClass> MachineClass::machine_classes;
std::map<std::string, MachineInterface *> MachineInterface::all_interfaces;

Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;
