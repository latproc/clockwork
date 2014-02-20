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

#include "CopyPropertiesAction.h"
#include "MachineInstance.h"
#include "Logger.h"

CopyPropertiesActionTemplate::CopyPropertiesActionTemplate(Value source, Value destination)
: source_name(source.asString()), dest_name(destination.asString()) {
}

CopyPropertiesActionTemplate::~CopyPropertiesActionTemplate() {
}
                                           
Action *CopyPropertiesActionTemplate::factory(MachineInstance *mi) {
	return new CopyPropertiesAction(mi, this);
}

CopyPropertiesAction::CopyPropertiesAction(MachineInstance *m, const CopyPropertiesActionTemplate *dat)
    : Action(m), source(dat->source_name), dest(dat->dest_name), source_machine(0),dest_machine(0) {
}

CopyPropertiesAction::CopyPropertiesAction() : source_machine(0), dest_machine(0) {
}

std::ostream &CopyPropertiesAction::operator<<(std::ostream &out) const {
	return out << "Copy Properties Action " << source << " to " << dest << "\n";
}

Action::Status CopyPropertiesAction::run() {
	owner->start(this);
    if (!source_machine)
        source_machine = owner->lookup(source);
    if (!dest_machine)
        dest_machine = owner->lookup(dest);
	if (source_machine && dest_machine) {
        //dest_machine->properties.add(source_machine->properties);
        SymbolTableConstIterator iter = source_machine->properties.begin();
        while (iter != source_machine->properties.end()) {
            const std::string &prop = (*iter).first;
            if (prop != "STATE" && prop != "NAME") {
                dest_machine->setValue( prop, (*iter).second);
            }
            iter++;
        }
        status = Complete;
	}
    else
        status = Failed;
    owner->stop(this);
	return status;
}

Action::Status CopyPropertiesAction::checkComplete() {
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

