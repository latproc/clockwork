#ifndef __MACHINEDETAILS_H__
#define __MACHINEDETAILS_H__

#include <string>
#include <list>
#include "Parameter.h"
#include <symboltable.h>
#include "MachineInstance.h"

// during the parsing process we build a list of
// things to instantiate but don't actually do it until
// all files have been loaded.

class MachineDetails {
    std::string machine_name;
    std::string machine_class;
    std::list<Parameter> parameters;
    std::string source_file;
    int source_line;
    SymbolTable properties;
    MachineInstance::InstanceType instance_type;

    MachineDetails(const char *nam, const char *cls,
                   std::list<Parameter> &params, const char *sf, int sl,
                   SymbolTable &props, MachineInstance::InstanceType kind);
    MachineInstance *instantiate();
};

#endif
