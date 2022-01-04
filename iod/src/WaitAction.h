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

#ifndef _WAIT_ACTION_H
#define _WAIT_ACTION_H 1

#include "Action.h"
#include "State.h"
#include "symboltable.h"
#include <iostream>

class MachineInstance;

struct WaitActionTemplate : public ActionTemplate {
    WaitActionTemplate(CStringHolder property);
    WaitActionTemplate(Value &val);
    WaitActionTemplate(long delta);
    virtual Action *factory(MachineInstance *mi) override;
    virtual void toC(std::ostream &out, std::ostream &vars) const override;
    virtual bool canBlock() const override { return true; }
    std::ostream &operator<<(std::ostream &out) const override;
    long wait_time;
    std::string property_name;
};

struct WaitAction : public Action {
    WaitAction(MachineInstance *mi, WaitActionTemplate &wat);
    ~WaitAction();
    Status run() override;
    Status checkComplete() override;
    virtual std::ostream &operator<<(std::ostream &out) const override;
    virtual bool usesTimer() { return true; }

    long wait_time;
    std::string property_name;
    bool use_property;
};

struct WaitForActionTemplate : public ActionTemplate {
    WaitForActionTemplate(CStringHolder targ, Value value);
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << target.get() << " to be " << value;
    }
    CStringHolder target;
    Value value;
};

struct WaitForAction : public Action {
    WaitForAction(MachineInstance *mi, WaitForActionTemplate &wat)
        : Action(mi), target(wat.target), value(wat.value) {}
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;
    CStringHolder target;
    Value value;
    MachineInstance *machine;
};

#endif
