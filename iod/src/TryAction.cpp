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

#include "TryAction.h"
#include "MachineCommandAction.h"
#include "MachineInstance.h"

TryActionTemplate::TryActionTemplate(Predicate *pred, MachineCommandTemplate *body,
                                     MachineCommandTemplate *handler)
    : condition(pred), body(body), handler(handler) {}

TryActionTemplate::~TryActionTemplate() {
    if (body) {
        delete body;
    }
    if (handler) {
        delete handler;
    }
}

Action *TryActionTemplate::factory(MachineInstance *mi) { return new TryAction(mi, this); }

std::ostream &TryActionTemplate::operator<<(std::ostream &out) const {
    return out << "TRY body=" << *body << " handler=" << *handler;
}

TryAction::TryAction(MachineInstance *mi, TryActionTemplate *t)
    : Action(mi), condition(t->condition),
      body(dynamic_cast<MachineCommand *>(t->body->factory(mi))),
      handler(dynamic_cast<MachineCommand *>(t->handler->factory(mi))) {}

TryAction::~TryAction() {
    if (body) {
        body->release();
    }
    if (handler) {
        handler->release();
    }
}

Action::Status TryAction::run() {
    owner->start(this);
    status = (*body)();
    if (status == Complete || status == Failed) {
        owner->stop(this);
    }
    else {
        // The body is blocking (Running or Suspended). Keep this action Running
        // (not Suspended) so checkComplete() is called each tick and can watch
        // the timeout predicate.
        status = Running;
    }
    return status;
}

Action::Status TryAction::checkComplete() {
    if (status == Complete || status == Failed) {
        return status;
    }
    // Timeout fired while the body is still running: abort the body and run the
    // handler (ABORT / THROW / RETURN).
    if (condition(owner)) {
        body->abort();
        status = (*handler)();
        if (handler->aborted()) {
            // Propagate the handler's ABORT/THROW/RETURN up (like IfCommandAction).
            abort();
        }
        if (status == Complete || status == Failed) {
            owner->stop(this);
        }
        return status;
    }
    // Body finished on its own: propagate its status.
    if (body->complete()) {
        status = body->getStatus();
        owner->stop(this);
        return status;
    }
    return Running;
}

std::ostream &TryAction::operator<<(std::ostream &out) const {
    return out << "TryAction body=" << *body << " handler=" << *handler;
}
