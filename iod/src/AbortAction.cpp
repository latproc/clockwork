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

#include "AbortAction.h"
#include "MachineInstance.h"
#include "Logger.h"
#include "DebugExtra.h"

Action *AbortActionTemplate::factory(MachineInstance *mi) {
	return new AbortAction(mi, this);
}

std::ostream &AbortAction::operator<<(std::ostream &out) const {
    out << "Abort Action\n";
    return out;
}

Action::Status AbortAction::run() {
	owner->start(this);
	status =  (abort_fail) ? Failed : Complete;
	owner->stop(this);
	return status;
}

Action::Status AbortAction::checkComplete() {
	if (status == Complete || status == Failed) return status;
	status =  (abort_fail) ? Failed : Complete;
	return status;
}

