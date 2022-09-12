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

#pragma once

#include "Action.h"
#include "Expression.h"
#include "Message.h"
#include "State.h"
#include "symboltable.h"

class MachineInstance;

enum class StateChangeReason { forced, automatic, transition };

struct SetStateActionTemplate : public ActionTemplate {
    SetStateActionTemplate(CStringHolder target, Value newstate,
                           StateChangeReason reason = StateChangeReason::forced);
    SetStateActionTemplate(CStringHolder target, Predicate *expr,
                           StateChangeReason reason = StateChangeReason::forced);

    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;
    void toC(std::ostream &out, std::ostream &vars) const override;
    bool canBlock() const override { return true; }

    const Predicate *expression() { return expr; }
    CStringHolder target;
    Value new_state;
    std::string trigger_event;
    StateChangeReason reason;

  private:
    Predicate *expr = nullptr;
    Condition condition;
};

struct SetStateAction : public Action {
    SetStateAction(MachineInstance *mi, SetStateActionTemplate &t, uint64_t auth = 0);
    Status run() override;
    Status checkComplete() override;
    void setAuthority(uint64_t auth) { authority = auth; }

    Condition &getCondition() { return condition; }

    std::ostream &operator<<(std::ostream &out) const override;
    CStringHolder target;
    Value saved_state;
    Value new_state; // new state as given by the program (could be a property name)
    State value;
    MachineInstance *machine;

  private:
    Predicate *expr = nullptr;
    Condition condition;
    StateChangeReason reason;

  protected:
    uint64_t authority;
    Status executeStateChange(bool use_transitions);
};

struct MoveStateActionTemplate : public SetStateActionTemplate {
    MoveStateActionTemplate(CStringHolder target, Value newstate)
        : SetStateActionTemplate(target, newstate) {}
    Action *factory(MachineInstance *mi) override;
};

struct MoveStateAction : public SetStateAction {
    MoveStateAction(MachineInstance *mi, MoveStateActionTemplate &t, uint64_t auth = 0)
        : SetStateAction(mi, t, auth) {}
    ~MoveStateAction() override;
    std::ostream &operator<<(std::ostream &out) const override;
    Status run() override;
};

struct SetIOStateAction : public Action {
    SetIOStateAction(MachineInstance *mi, IOComponent *io, const State &new_state,
                     uint64_t auth = 0)
        : Action(mi), io_interface(io), state(new_state), authority(auth) {}
    Status run() override;
    Status checkComplete() override;
    void setAuthority(uint64_t auth) { authority = auth; }
    std::ostream &operator<<(std::ostream &out) const override;
    IOComponent *io_interface;
    State state;
    uint64_t authority;
};
