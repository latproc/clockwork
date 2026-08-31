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
#include "Scheduler.h"

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

TryTimeoutAction::TryTimeoutAction(MachineInstance *mi, TryAction *ta) : Action(mi), try_action(ta) {
    // Keep the TryAction alive until this interrupt fires (or is dropped), so a
    // body that completes before the timeout does not leave a dangling pointer.
    if (try_action) {
        try_action->retain();
    }
}

TryTimeoutAction::~TryTimeoutAction() {
    if (try_action) {
        try_action->release();
    }
}

Action::Status TryTimeoutAction::run() {
    owner->start(this);
    if (try_action) {
        try_action->timeoutTriggered();
    }
    status = Complete;
    owner->stop(this);
    return status;
}

Action::Status TryTimeoutAction::checkComplete() {
    assert(status == Complete);
    return status;
}

std::ostream &TryTimeoutAction::operator<<(std::ostream &out) const {
    return out << "TryTimeoutAction";
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
        return status;
    }
    // Body is blocking (Running or Suspended).
    if (condition(owner)) {
        // The timeout predicate is already true: run the handler immediately.
        runHandler();
        return status;
    }
    scheduleTimeout();
    status = Running; // keep this action Running so it stays re-checked
    return status;
}

Action::Status TryAction::checkComplete() {
    if (status == Complete || status == Failed) {
        return status;
    }
    if (timeout_triggered) {
        runHandler();
        return status;
    }
    // Fallback for the case where the timer interrupt is not scheduled (the
    // predicate was already true, or is not a TIMER clause we can schedule).
    if (condition(owner)) {
        runHandler();
        return status;
    }
    if (body->complete()) {
        status = body->getStatus();
        owner->stop(this);
        return status;
    }
    return Running;
}

void TryAction::timeoutTriggered() {
    if (status == Complete || status == Failed) {
        return;
    }
    body->abortCommand();
    timeout_triggered = true;
    // The machine's tick loop now reaches this action (it is the top of the
    // stack after the body is removed), and checkComplete() runs the handler.
}

void TryAction::scheduleTimeout() {
    if (!condition.predicate) {
        return;
    }
    PredicateTimerDetails *ptd = condition.predicate->scheduleTimerEvents(0, owner);
    if (ptd) {
        TryTimeoutAction *tta = new TryTimeoutAction(owner, this);
        Scheduler::instance()->add(new ScheduledItem(ptd->delay, tta));
        delete ptd;
    }
}

void TryAction::runHandler() {
    timeout_triggered = true;
    handler_started = true;
    status = (*handler)();
    if (handler->aborted()) {
        // Propagate the handler's ABORT/THROW/RETURN up (like IfCommandAction).
        abort();
    }
    if (status == Complete || status == Failed) {
        owner->stop(this);
    }
}

std::ostream &TryAction::operator<<(std::ostream &out) const {
    return out << "TryAction body=" << *body << " handler=" << *handler;
}
