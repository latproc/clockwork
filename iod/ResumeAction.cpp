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

#include "ResumeAction.h"
#include "MachineInstance.h"
#include "Logger.h"

Action *ResumeActionTemplate::factory(MachineInstance *mi) {
	return new ResumeAction(mi, this);
}

std::ostream &ResumeAction::operator<<(std::ostream &out) const {
	return out << "Resume Action " << machine_name << "\n";
}

Action::Status ResumeAction::run() {
	owner->start(this);
    if (machine_name == "ALL") {
        // this is actually a command to resume all local machines
        owner->resume(machine_name);
    }
    else {
        machine = owner->lookup(machine_name);
        if (machine) {
            if (resume_state.length() == 0)
                machine->resume();
            else
                machine->resume(resume_state);
        }
        else {
            std::string err("Error: cannot find machine: ");
            err = err + machine_name;
            error_str = strdup(err.c_str());
            status = Failed;
            return status;
        }
    }
	status = Complete;
    owner->stop(this);
	return status;
}

Action::Status ResumeAction::checkComplete() {
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

