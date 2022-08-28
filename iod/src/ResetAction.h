#pragma once

#include "Action.h"
#include <string>

class MachineInstance;

class ResetActionTemplate : public ActionTemplate {
  public:
    ResetActionTemplate() {}
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "ResetAction template "
                   << "\n";
    }
};

class ResetAction : public Action {
  public:
    ResetAction(MachineInstance *m) : Action(m) {}
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;
};
