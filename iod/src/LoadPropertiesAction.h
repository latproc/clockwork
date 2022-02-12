#pragma once

#include "Action.h"
#include <iostream>
#include <string>

class MachineInstance;

class LoadPropertiesActionTemplate : public ActionTemplate {
  public:
    explicit LoadPropertiesActionTemplate(const char *filename, const Value &property)
        : values_file(filename), property_name(property) {}
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "Load Properties template "
                   << "\n";
    }
    const std::string values_file;
    Value property_name;
};

class LoadPropertiesAction : public Action {
  public:
    LoadPropertiesAction(MachineInstance *m, const LoadPropertiesActionTemplate &lpat)
        : Action(m), machine(m), values_file(lpat.values_file), property_name(lpat.property_name) {}
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;
    MachineInstance *machine;
    const std::string values_file;
    Value property_name;
};
