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

#include "ShutdownAction.h"
#include "MachineInstance.h"
#include "Logger.h"

extern bool program_done;

Action *ShutdownActionTemplate::factory(MachineInstance *mi) {
	return new ShutdownAction(mi);
}

std::ostream &ShutdownAction::operator<<(std::ostream &out) const {
	return out << "Shutdown Action " << "\n";
}

Action::Status ShutdownAction::run() {
	owner->start(this);
	program_done = true;
	status = Complete;
	result_str = "OK";
	owner->stop(this);
	return status;
}

Action::Status ShutdownAction::checkComplete() {
	status = Complete;
	result_str = "OK";
	return status;
}

