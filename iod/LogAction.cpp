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

#include "LogAction.h"
#include "Logger.h"
#include "MachineInstance.h"

Action *LogActionTemplate::factory(MachineInstance *mi) {
    return new LogAction(mi, *this);
}

Action::Status LogAction::run() {
	owner->start(this);
    std::cout << "------- " << owner->getName() << ": " << message << " -------\n";
    DBG_MSG << "------- " << owner->getName() << ": " << message << " -------\n";
	status = Complete;
	owner->stop(this);
	return status;
}

Action::Status LogAction::checkComplete() {
	return status;
}

std::ostream &LogAction::operator<<(std::ostream &out)const {
    return out << "LOG " << message;
}

