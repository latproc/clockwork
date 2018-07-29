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
#include "MessageLog.h"

Action *LogActionTemplate::factory(MachineInstance *mi) {
    return new LogAction(mi, *this);
}

std::ostream &LogActionTemplate::operator<<(std::ostream &out) const {
	return out << "LOG " << message;
}


Action::Status LogAction::run() {
	owner->start(this);
    std::stringstream ss;
    if (!predicate) {
        Value prop(owner->getValue(message.asString()));
        if (prop != SymbolTable::Null) {
            ss << "------- " << owner->fullName() << ": " << prop << " -------";
        }
        else {
            ss << "------- " << owner->fullName() << ": " << message << " -------";
        }
    }
    else {
        Value val = predicate->evaluate(owner);
        ss << "------- " << owner->fullName() << ": " << val << " -------";
    }
    std::cout << ss.str() << "\n";
    MessageLog::instance()->add(ss.str().c_str());
	status = Complete;
	owner->stop(this);
	return status;
}

Action::Status LogAction::checkComplete() {
	return status;
}

std::ostream &LogAction::operator<<(std::ostream &out)const {
	if (!predicate) {
		return out << "LOG " << message;
	}
	Value val = predicate->evaluate(owner);
	return out << "LOG " << val;
}

