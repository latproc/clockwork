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

#include "IfCommandAction.h"
#include "MachineInstance.h"
#include "Logger.h"
#include "MachineCommandAction.h"
#include "DebugExtra.h"

Action *IfCommandActionTemplate::factory(MachineInstance *mi) {
	return new IfCommandAction(mi, this);
}

std::ostream &IfCommandActionTemplate::operator<<(std::ostream &out) const {
	return out << "IF template "  << *condition.predicate << ": " << *command << "\n";
}


std::ostream &IfCommandAction::operator<<(std::ostream &out) const {
	return out << "If Action " << *condition.predicate << ": " << *command << "\n";
}

IfCommandAction::IfCommandAction(MachineInstance *mi, IfCommandActionTemplate *t) : Action(mi), condition(t->condition)  { 
	command = dynamic_cast<MachineCommand*>(t->command->factory(mi));
}

Action::Status IfCommandAction::run() {
	owner->start(this);
	//DBG_M_ACTIONS << "If is testing " << *condition.predicate << "\n";
	if (!condition(owner)) {
		DBG_M_ACTIONS << owner->getName() << " IF " << *condition.predicate << " returned false" << "\n";
		status = Complete;
		owner->stop(this);
		return status;
	}
	else {
		DBG_M_ACTIONS << owner->getName() << " IF " << *condition.predicate<< " returned true" << "\n";
    }
	status = (*command)();
	if (status == Complete || status == Failed) {
		owner->stop(this);
	}
	return status;
}

Action::Status IfCommandAction::checkComplete() {
	if (status == Complete || status == Failed) return status;
	if (this != owner->executingCommand()) {
		//DBG_MSG << "checking complete on " << *this << " when it is not the top of stack \n";
	}
	else {
		status = Complete;
		owner->stop(this);
	}
	return status;
}

Action *IfElseCommandActionTemplate::factory(MachineInstance *mi) {
	return new IfElseCommandAction(mi, this);
}

std::ostream &IfElseCommandActionTemplate::operator<<(std::ostream &out) const {
	return out << "IF template "  << *condition.predicate << ": " << *command << "\n";
}

std::ostream &IfElseCommandAction::operator<<(std::ostream &out) const {
	return out << "IfElse Action " << *condition.predicate << ": " << *command << "\n";
}

IfElseCommandAction::IfElseCommandAction(MachineInstance *mi, IfElseCommandActionTemplate *t) : Action(mi), condition(t->condition)  { 
	command = dynamic_cast<MachineCommand*>(t->command->factory(mi));
	else_command = dynamic_cast<MachineCommand*>(t->else_command->factory(mi));
}

Action::Status IfElseCommandAction::run() {
	owner->start(this);
	//DBG_MSG << "IfElse is testing " << *condition.predicate << "\n";
	if (condition(owner)) {
		//DBG_MSG << "false" << "\n";
		status = (*command)();
	}
	else
		//DBG_MSG << "true" << "\n";
		status = (*else_command)();
	if (status == Complete || status == Failed) {
		owner->stop(this);
	}
	return status;
}

Action::Status IfElseCommandAction::checkComplete() {
	if (status == Complete || status == Failed) return status;
	if (this != owner->executingCommand()) {
		//DBG_MSG << "checking complete on " << *this << " when it is not the top of stack \n";
	}
	else {
		status = Complete;
		owner->stop(this);
	}
	return status;
}
