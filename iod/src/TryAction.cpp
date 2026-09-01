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

TryActionTemplate::TryActionTemplate(Value timeout_value, MachineCommandTemplate *body,
                                     MachineCommandTemplate *timeout_handler)
    : timeout_value(timeout_value), body(body), timeout_handler(timeout_handler) {}

TryActionTemplate::~TryActionTemplate() {
    if (body) {
        delete body;
    }
    if (timeout_handler) {
        delete timeout_handler;
    }
}

Action *TryActionTemplate::factory(MachineInstance *mi) { return new TryAction(mi, this); }

std::ostream &TryActionTemplate::operator<<(std::ostream &out) const {
    out << "TRY body=" << *body;
    if (timeout_handler) {
        out << " handler=" << *timeout_handler;
    }
    return out;
}

TryTimeoutAction::TryTimeoutAction(MachineInstance *mi, TryAction *ta) : Action(mi), try_action(ta) {
    // Keep the TryAction alive until this interrupt fires (or is dropped), so a
    // body that completes before the deadline does not leave a dangling pointer.
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
    : Action(mi), timeout_value(t->timeout_value),
      body(dynamic_cast<MachineCommand *>(t->body->factory(mi))),
      timeout_handler(t->timeout_handler
                          ? dynamic_cast<MachineCommand *>(t->timeout_handler->factory(mi))
                          : nullptr) {}

TryAction::~TryAction() {
    if (body) {
        body->release();
    }
    if (timeout_handler) {
        timeout_handler->release();
    }
}

Action::Status TryAction::run() {
    owner->start(this);
    // Resolve the deadline duration: an integer literal is used directly; a
    // symbol is looked up on the machine (an OPTION). Invalid values are caught
    // at load.
    if (timeout_value.kind == Value::t_integer) {
        timeout_ms = timeout_value.iValue;
    }
    else if (timeout_value.kind == Value::t_symbol) {
        Value v = owner->getValue(timeout_value.sValue);
        if (v.kind == Value::t_integer) {
            timeout_ms = v.iValue;
        }
    }
    status = (*body)();
    if (status == Complete || status == Failed) {
        owner->stop(this);
        return status;
    }
    // Body is blocking.
    if (timeout_ms <= 0) {
        // Zero/negative timeout: run the handler immediately.
        runTimeoutHandler();
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
        runTimeoutHandler();
        return status;
    }
    if (body->complete()) {
        if (body->getStatus() == Failed) {
            status = Failed;
            owner->stop(this);
            return status;
        }
        // Body completed. Record its completion time (once) and compare against
        // the absolute deadline: completion succeeds only if it is strictly
        // earlier; completion at or after the deadline loses to the timeout.
        // This closes the window where the scheduler's interrupt has not been
        // dequeued yet even though the deadline has already passed.
        if (completion_us == 0) {
            completion_us = microsecs();
        }
        if (deadline_us == 0 || completion_us < deadline_us) {
            status = body->getStatus();
            owner->stop(this);
            return status;
        }
        runTimeoutHandler();
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
    // Record the absolute deadline so checkComplete() can decide a completion
    // race against it rather than against whichever event the scheduler dequeues
    // first (timeout-spec.md "Completion Races").
    uint64_t start_us = microsecs();
    deadline_us = start_us + timeout_ms * 1000;
    TryTimeoutAction *tta = new TryTimeoutAction(owner, this);
    Scheduler::instance()->add(new ScheduledItem(start_us, timeout_ms * 1000, tta));
}

void TryAction::abortActive() {
    // An enclosing scope's deadline fired while this scope was active: abort
    // whichever nested work is currently running (the recovery handler if it has
    // started, otherwise the body).
    if (handler_started && timeout_handler) {
        timeout_handler->abortCommand();
    }
    else if (body) {
        body->abortCommand();
    }
}

void TryAction::runTimeoutHandler() {
    timeout_triggered = true;
    if (timeout_handler) {
        handler_started = true;
        // timeout-spec.md: TIMEOUT is readable inside the ON TIMEOUT block.
        long saved_context = getTimeoutContext();
        setTimeoutContext(timeout_ms);
        status = (*timeout_handler)();
        setTimeoutContext(saved_context);
        if (timeout_handler->aborted()) {
            // Propagate the handler's ABORT/THROW/RETURN up (like IfCommandAction).
            abort();
        }
        if (status == Complete || status == Failed) {
            owner->stop(this);
        }
    }
    else {
        // No ON TIMEOUT block: an unhandled timeout fails the scope.
        abort();
        status = Failed;
        setError("timed out");
        owner->stop(this);
    }
}

std::ostream &TryAction::operator<<(std::ostream &out) const {
    out << "TryAction body=" << *body;
    if (timeout_handler) {
        out << " handler=" << *timeout_handler;
    }
    return out;
}
