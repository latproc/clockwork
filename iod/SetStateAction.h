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

#ifndef __SETSTATE_ACTION__
#define __SETSTATE_ACTION__ 1

#include <iostream>
#include "Action.h"
#include "symboltable.h"
#include "Message.h"
#include "State.h"
#include "Expression.h"

class MachineInstance;

struct SetStateActionTemplate : public ActionTemplate {
    SetStateActionTemplate(CStringHolder targ, State newstate) : target(targ), value(newstate) { }
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << target.get() << " " << value.getName();
    }
    CStringHolder target;
    State value;
    std::string trigger_event;
	Condition condition;
};

struct SetStateAction : public Action {
    SetStateAction(MachineInstance *mi, SetStateActionTemplate &t) 
	    : Action(mi), target(t.target), value(t.value), machine(0) { }
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;
    CStringHolder target;
    State value;
    MachineInstance *machine;
	Condition condition;
protected:
	Status executeStateChange(bool use_transitions);
};

struct MoveStateActionTemplate : public SetStateActionTemplate {
    MoveStateActionTemplate(CStringHolder targ, State newstate) : SetStateActionTemplate(targ, newstate) { }
    virtual Action *factory(MachineInstance *mi);
};

struct MoveStateAction : public SetStateAction {
	MoveStateAction(MachineInstance *mi, MoveStateActionTemplate &t) : SetStateAction(mi, t){ }
	~MoveStateAction();
	std::ostream &operator<<(std::ostream &out) const;
    Status run();
};

struct SetIOStateAction : public Action {
    SetIOStateAction(MachineInstance *mi, IOComponent *io, const State& new_state) 
	    : Action(mi), io_interface(io), state(new_state) {}
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;
	IOComponent *io_interface;
	State state;
};

#endif