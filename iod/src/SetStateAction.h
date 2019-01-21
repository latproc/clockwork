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
    SetStateActionTemplate(CStringHolder targ, Value newstate) : target(targ), new_state(newstate) { }
    virtual Action *factory(MachineInstance *mi) override;
		virtual std::ostream &operator<<(std::ostream &out) const override;
    virtual void toC(std::ostream &out, std::ostream &vars) const override;
    virtual bool canBlock() const override { return true; }

    CStringHolder target;
    Value new_state;
    std::string trigger_event;
private:
	Condition condition;
};

struct SetStateAction : public Action {
	SetStateAction(MachineInstance *mi, SetStateActionTemplate &t, uint64_t auth = 0);
    Status run();
    Status checkComplete();
	void setAuthority(uint64_t auth) { authority = auth; }

	Condition &getCondition() { return condition; }

	virtual std::ostream &operator<<(std::ostream &out)const;
    CStringHolder target;
		Value saved_state;
    Value new_state; // new state as given by the program (may be a property name)
    State value;
    MachineInstance *machine;
private:
	Condition condition;
protected:
	uint64_t authority;
	Status executeStateChange(bool use_transitions);
};

struct MoveStateActionTemplate : public SetStateActionTemplate {
    MoveStateActionTemplate(CStringHolder targ, Value newstate) : SetStateActionTemplate(targ, newstate) { }
    virtual Action *factory(MachineInstance *mi);
};

struct MoveStateAction : public SetStateAction {
	MoveStateAction(MachineInstance *mi, MoveStateActionTemplate &t, uint64_t auth = 0) : SetStateAction(mi, t, auth){ }
	~MoveStateAction();
	std::ostream &operator<<(std::ostream &out) const;
    Status run();
};

struct SetIOStateAction : public Action {
    SetIOStateAction(MachineInstance *mi, IOComponent *io, const State& new_state, uint64_t auth = 0)
	    : Action(mi), io_interface(io), state(new_state), authority(auth) {}
    Status run();
    Status checkComplete();
	void setAuthority(uint64_t auth) { authority = auth; }
    virtual std::ostream &operator<<(std::ostream &out)const;
	IOComponent *io_interface;
	State state;
	uint64_t authority;
};

#endif
