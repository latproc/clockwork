#pragma once

#include "Action.h"
#include <iostream>
#include <string>

class MachineInstance;

class LoadPropertiesActionTemplate : public ActionTemplate {
  public:
    explicit LoadPropertiesActionTemplate(const char *filename, const Value &property)
        : values_file(filename), property_name(property) {}
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override {
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
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;
    MachineInstance *machine;
    const std::string values_file;
    Value property_name;
};
