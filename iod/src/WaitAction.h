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

#include <iostream>
#include "Action.h"
#include "symboltable.h"
#include "State.h"

class MachineInstance;

struct WaitActionTemplate : public ActionTemplate {
    WaitActionTemplate(CStringHolder property) : wait_time(-1), property_name(property.get()) { }
    WaitActionTemplate(Value &val) : wait_time(-1), property_name("") {
		if (val.kind == Value::t_integer)
			wait_time = val.iValue;
		else
			property_name = val.sValue;
	}
    WaitActionTemplate(long delta) : wait_time(delta), property_name("") { }
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << wait_time;
    }
    long wait_time;
    std::string property_name;
};

struct WaitAction : public Action {
    WaitAction(MachineInstance *mi, WaitActionTemplate &wat);
	~WaitAction();
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;
	virtual bool usesTimer() { return true; }

    struct timeval start_time;
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
    virtual std::ostream &operator<<(std::ostream &out)const;
    CStringHolder target;
    Value value;
    MachineInstance *machine;
};

#endif
