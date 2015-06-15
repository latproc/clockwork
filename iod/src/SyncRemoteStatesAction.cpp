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

#include "SyncRemoteStatesAction.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "IOComponent.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessageEncoding.h"
#include "Channel.h"

Action *SyncRemoteStatesActionTemplate::factory(MachineInstance *mi)
{ 
	return new SyncRemoteStatesAction(mi, *this);
}

Action::Status SyncRemoteStatesAction::execute()
{
	owner->start(this);
	result_str = "OK";
	status = Running;
	Channel *chn = dynamic_cast<Channel*>(owner);
	assert(chn);
	chn->syncRemoteStates();
	status = Complete;
	owner->stop(this);
	return status;
#if 0
	std::stringstream ss;
	ss << owner->getName() << " failed to find machine " << target.get() << " for SetState action" << std::flush;
	std::string str = ss.str();
	error_str = strdup(str.c_str());
	status = Failed;
	owner->stop(this);
	return status;
#endif
}


Action::Status SyncRemoteStatesAction::run() {
	execute();
	return Running;
}

Action::Status SyncRemoteStatesAction::checkComplete() {
	if (status == New || status == NeedsRetry) { execute(); }
	if (status == Suspended) resume();
	if (status != Running) return status;

	return status;
}

std::ostream &SyncRemoteStatesAction::operator<<(std::ostream &out) const {
	return out << "SyncRemoteStatesAction";
}
