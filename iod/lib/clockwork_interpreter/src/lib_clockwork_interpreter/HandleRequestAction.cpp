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

#include "HandleRequestAction.h"
// // #include "DebugExtra.h"
// // #include "Logger.h"
#include "IOComponent.h"
#include "MachineInstance.h"
// // #include "MessageLog.h"

Action *HandleRequestActionTemplate::factory(MachineInstance *mi) {
    return new HandleRequestAction(mi, *this);
}

Action::Status HandleRequestAction::run() { return checkComplete(); }

Action::Status HandleRequestAction::checkComplete() {
    if (status == New || status == NeedsRetry) {
        owner->start(this);
        safeSend(*sock, request.c_str(), request.length(), header);
        status = Running;
    } else if (status == Suspended) {
        resume();
    }
    if (status != Running) {
        return status;
    }

    char *buf;
    size_t len;
    if (safeRecv(*sock, &buf, &len, false, 0, header)) {
        result_str = buf;
        delete[] buf;
        status = Complete;
        owner->stop(this);
    }

    return status;
}
std::ostream &HandleRequestAction::operator<<(std::ostream &out) const {
    return out << "HandleRequestAction " << request;
}
