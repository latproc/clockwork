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

#include "EnableAction.h"
#include "MachineInstance.h"
#include "Logger.h"

EnableActionTemplate::EnableActionTemplate(const std::string &name, const char *property, const Value *val)
: machine_name(name), property_name(0), property_value(0) {
    if (property && val) {
        property_name = strdup(property);
        property_value = strdup(val->asString().c_str());
    }
}

EnableActionTemplate::~EnableActionTemplate() {
    delete property_name;
    delete property_value;
}
                                           
Action *EnableActionTemplate::factory(MachineInstance *mi) {
	return new EnableAction(mi, this);
}

EnableAction::EnableAction(MachineInstance *m, const EnableActionTemplate *dat)
    : Action(m), machine_name(dat->machine_name), machine(0), property_name(0), property_value(0) {
        if (dat->property_name) property_name = strdup(dat->property_name);
        if (dat->property_value) property_value = strdup(dat->property_value);
}

EnableAction::EnableAction() {
    delete property_name;
    delete property_value;
}

std::ostream &EnableAction::operator<<(std::ostream &out) const {
	return out << "Enable Action " << machine_name << "\n";
}

Action::Status EnableAction::run() {
	owner->start(this);
	machine = owner->lookup(machine_name);
	if (machine) {
		machine->enable();
	}
	status = Complete;
	if (status == Complete || status == Failed) {
		owner->stop(this);
	}
	return status;
}

Action::Status EnableAction::checkComplete() {
	if (status == Complete || status == Failed) return status;
	if (this != owner->executingCommand()) {
		DBG_MSG << "checking complete on " << *this << " when it is not the top of stack \n";
	}
	else {
		status = Complete;
		owner->stop(this);
	}
	return status;
}

