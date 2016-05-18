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

#include <stdio.h>
#include "HandleMessageAction.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "IOComponent.h"
#include "MachineInstance.h"

Action *HandleMessageActionTemplate::factory(MachineInstance *mi) {
	return new HandleMessageAction(mi, *this);
}

HandleMessageAction::~HandleMessageAction() {
    if (handler)
        handler->release();
}


Action::Status HandleMessageAction::run() {
	owner->start(this);
	DBG_M_MESSAGING << "Handling message " << *package.message << " from "
		<< package.transmitter->getName() << " to " << owner->getName() << "\n";

	if (owner->io_interface && package.transmitter == owner->io_interface) {
		Status sent = owner->execute(package.message, package.transmitter);
		if (sent == Failed)
			status = Failed;
		else {
			status = Complete;
		}
		owner->stop(this);
		return status;
	}

	handler = owner->findHandler(*package.message, package.transmitter, package.needs_receipt);
	if (handler) {
		suspend();
		Status stat = (*handler)();
		if ( stat == Failed ) {
			std::stringstream ss;
			ss << "handler " << *handler << " failed to start\n" << std::flush;
			error_str = strdup(ss.str().c_str());
			status = Failed;
			owner->stop(this);
			return status;
		}
		else if ( stat == Running || stat == Suspended){
			result_str = "OK";
			status = stat;
			return status;
		}
		else {
			// the command we started may have been suspended by running
			// other commands.  if so we continue, otherwise we are done
			if (stat == Complete) {
				owner->stop(this);
			}
			status = stat;
			return status;
		}
	}
    if (owner->debug()) {
        char buf[100];
        snprintf(buf, 100, "no handler for handling %s\n", package.message->getText().c_str() );
        DBG_M_MESSAGING << buf;
        result_str = strdup(buf);
    }
	status = Complete;
	owner->stop(this);
	return status;
}

Action::Status HandleMessageAction::checkComplete() {
	if (status == Complete || status == Failed) return status;
	if (status == Suspended) resume();
	if (handler && status == Running) { 
		(void)handler->complete(); 
		status = handler->getStatus(); 
	}
	if (status == Complete || status == Failed) {
		owner->stop(this);
	}
	return status;
}

std::ostream &HandleMessageAction::operator<<(std::ostream &out)const {
	return out << "HandleMessage with package " << *(package.message);
}

