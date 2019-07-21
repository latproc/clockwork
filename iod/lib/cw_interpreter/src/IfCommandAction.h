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

#ifndef __IfComandAction_h__
#define __IfComandAction_h__ 1

#include <iostream>
#include "Expression.h"
#include "Action.h"

class MachineCommandTemplate;
class MachineCommand;
class MachineInstance;

struct IfCommandActionTemplate : public ActionTemplate {
	~IfCommandActionTemplate() {}
	IfCommandActionTemplate(Predicate *pred, MachineCommandTemplate *mct) : condition(pred), command(mct) {}
    virtual Action *factory(MachineInstance *mi);
	std::ostream &operator<<(std::ostream &out) const;

	Condition condition;
	MachineCommandTemplate *command;
};

struct IfCommandAction : public Action {
	IfCommandAction(MachineInstance *mi, IfCommandActionTemplate *t);
	~IfCommandAction();
    Status runActions();
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;

	Condition condition;
	MachineCommand *command;
};

struct IfElseCommandActionTemplate : public  ActionTemplate {
	IfElseCommandActionTemplate(Predicate *pred, MachineCommandTemplate *mct, MachineCommandTemplate *else_clause)
	 : condition(pred), command(mct), else_command(else_clause) {}
    virtual Action *factory(MachineInstance *mi);
	std::ostream &operator<<(std::ostream &out) const;

	Condition condition;
	MachineCommandTemplate *command;
	MachineCommandTemplate *else_command;
};

struct IfElseCommandAction : public Action {
	IfElseCommandAction(MachineInstance *mi, IfElseCommandActionTemplate *t);
	~IfElseCommandAction();
    Status runActions();
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;

	Condition condition;
	MachineCommand *command;
	MachineCommand *else_command;

};
#endif
