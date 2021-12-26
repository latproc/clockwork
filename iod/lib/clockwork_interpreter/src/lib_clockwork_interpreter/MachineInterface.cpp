
#include "MachineInterface.h"
#include "includes.hpp"
// #include "Logger.h"

std::map<std::string, MachineInterface *> MachineInterface::all_interfaces;

MachineInterface::MachineInterface(const char *class_name)
    : MachineClass(class_name) {
    if (all_interfaces.count(class_name) != 0) {
        NB_MSG << "interface " << class_name << " is already defined\n";
    }
    all_interfaces[class_name] = this;
}

MachineInterface::~MachineInterface() { all_interfaces.erase(this->name); }
/*
    void MachineInterface::addProperty(const char *name) {
    property_names.insert(name);
    }

    void MachineInterface::addCommand(const char *name) {
    command_names.insert(name);
    }
*/
