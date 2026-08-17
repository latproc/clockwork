/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc

    Latproc is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    Latproc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Latproc; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __MACHINECOMMAND_ACTION
#define __MACHINECOMMAND_ACTION 1

#include "Action.h"
#include "Expression.h"
#include "symboltable.h"
#include <string>
#include <vector>

/*
    A MachineCommand executes a list of actions,
    usage example:

    MachineCommand f;
    f.addAction(new WaitAction,new ActionParameterList(1));
    f.addAction(new SetStateAction,new ActionParameterList(output, "on"));
    Action::Status status = *(f);
    if (status == Action::Error) {
        // handle error
    }
    else {
    }
*/

class MachineInstance;

class MachineCommandTemplate : public ActionTemplate {
  public:
    MachineCommandTemplate(CStringHolder cmd_name, CStringHolder state, bool auto_switch = false);
    MachineCommandTemplate(const MachineCommandTemplate &) = delete;
    MachineCommandTemplate &operator=(const MachineCommandTemplate &) = delete;
    ~MachineCommandTemplate() override;
    virtual Action *factory(MachineInstance *mi);

    std::ostream &operator<<(std::ostream &out) const {
        return out << command_name.get() << " " << state_name.get();
    }
    void setActionTemplates(std::list<ActionTemplate *> &new_actions);
    void setActionTemplate(ActionTemplate *action);

    CStringHolder &getStateName() { return state_name; }
    const std::vector<std::string> &getWithinStates() const { return within_states; }
    void setWithinStates(std::vector<std::string> states);
    Condition *getGuard() const { return guard; }
    void setGuard(Predicate *p);

    bool matchesWithin(const std::string &state) const;
    bool sameWithinSet(const std::vector<std::string> &other) const;
    bool sameGuard(const Predicate *other) const;
    bool isDuplicateOf(const std::vector<std::string> &within, const Predicate *g) const;
    void describeGuards(std::ostream &out) const;

    std::vector<ActionTemplate *> action_templates;
    CStringHolder command_name;

  private:
    CStringHolder state_name;
    std::vector<std::string> within_states;
    Condition *guard;

  public:
    long timeout;
    bool switch_state;
};

class MachineCommand : public Action {
  public:
    MachineCommand(MachineInstance *mi, MachineCommandTemplate *mct);
    ~MachineCommand();
    void addAction(Action *a, ActionParameterList *params);
    Status checkAction(Action *a, Status stat);
    Status runActions();
    Status run();
    Status checkComplete();
    void reset();
    void setActions(std::list<Action *> &new_actions);
    const std::string name() const { return command_name.get(); }
    const std::string stateName() const { return state_name.get(); }
    virtual std::ostream &operator<<(std::ostream &out) const;

    CStringHolder &getStateName() { return state_name; }
    bool autoSwitch() { return switch_state; }
    const std::vector<std::string> &getWithinStates() const { return within_states; }
    Condition *getGuard() const { return guard; }
    bool matchesWithin(const std::string &state) const;
    bool matches(MachineInstance *mi) const;

  private:
    std::vector<Action *> actions;
    size_t last_step, current_step;
    CStringHolder command_name;

    CStringHolder state_name;
    std::vector<std::string> within_states;
    Condition *guard;
    Trigger *timeout_trigger;
    bool switch_state;
};

#endif
