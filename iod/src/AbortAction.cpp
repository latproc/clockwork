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
#include "DebugExtra.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "SendMessageAction.h"

AbortActionTemplate::AbortActionTemplate(bool failed) : abort_fail(failed) {}

AbortActionTemplate::AbortActionTemplate(bool failed, Value exception_message)
    : abort_fail(true), message(exception_message.sValue) {}

Action *AbortActionTemplate::factory(MachineInstance *mi) { return new AbortAction(mi, this); }

AbortAction::AbortAction(MachineInstance *m, const AbortActionTemplate *dat)
    : Action(m), abort_fail(dat->abort_fail), message(dat->message) {}

std::ostream &AbortAction::operator<<(std::ostream &out) const {
    if (message.length()) {
        out << "Throw Exception (" << message << ")"
            << " to " << owner->getName();
    }
    else if (abort_fail) {
        out << "Abort";
    }
    else {
        out << "Return";
    }
    return out;
}

Action::Status AbortAction::run() {
    owner->start(this);
    if (message.length() > 0) {
        SendMessageActionTemplate smat(this->message.c_str(), owner);
        Action *sma = smat.factory(owner);
        (*sma)();
        delete sma;
    }
    abort();
    status = Complete;
    //    if (abort_fail) {
    //        status = Failed;
    //        error_str = strdup(message.c_str());
    //    }
    //    else {
    //        status = Complete;
    //    }
    owner->stop(this);
    return status;
}

Action::Status AbortAction::checkComplete() {
    if (status == Complete || status == Failed) {
        return status;
    }
    abort();
    if (abort_fail) {
        status = Failed;
        error_str = strdup(message.c_str());
    }
    else {
        status = Complete;
    }
    owner->stop(this);
    return status;
}
