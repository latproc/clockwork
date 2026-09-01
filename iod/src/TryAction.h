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

#pragma once

#include "Action.h"

class MachineCommandTemplate;
class MachineCommand;
class MachineInstance;

struct TryAction;

// Scheduled by a TryAction when its body blocks, so the deadline can interrupt
// the body even though the body's nested blocking action sits above the TryAction
// on the machine's action stack.
struct TryTimeoutAction : public Action {
    TryTimeoutAction(MachineInstance *mi, TryAction *ta);
    ~TryTimeoutAction() override;
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;

    TryAction *try_action;
};

// A timed scope per docs/timeout-spec.md:
//
//   <body> WITH TIMEOUT <duration> [ ON TIMEOUT { <statements> } ]
//
// Runs <body>. If it finishes before <duration> (milliseconds, from the scope's
// start) elapses, the scope completes with the body's status. Otherwise the body
// is aborted and the ON TIMEOUT block runs (or, with no ON TIMEOUT block, the
// timeout propagates to the enclosing timed scope).

struct TryActionTemplate : public ActionTemplate {
    TryActionTemplate(Value timeout_value, MachineCommandTemplate *body,
                      MachineCommandTemplate *timeout_handler);
    ~TryActionTemplate() override;
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;

    Value timeout_value;               // duration in ms (literal or variable)
    MachineCommandTemplate *body;
    MachineCommandTemplate *timeout_handler; // null if no ON TIMEOUT block
};

struct TryAction : public Action {
    TryAction(MachineInstance *mi, TryActionTemplate *t);
    ~TryAction() override;
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;

    // Called by TryTimeoutAction when the timer fires: abort the body and flag
    // that the handler must run (checkComplete() does the actual handler run).
    void timeoutTriggered();
    void scheduleTimeout();
    void runTimeoutHandler();
    // Abort the active nested work (body, or the running recovery handler).
    void abortActive() override;

    Value timeout_value;
    long timeout_ms = 0; // resolved from timeout_value in run()
    MachineCommand *body;
    MachineCommand *timeout_handler;
    bool handler_started = false;
    bool timeout_triggered = false;
};
