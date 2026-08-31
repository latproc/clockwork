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
#include "Expression.h"

class MachineCommandTemplate;
class MachineCommand;
class MachineInstance;

// TRY { <body> } WHEN <predicate> { <handler> }
//
// Runs <body>. If <body> finishes before the predicate (e.g. TIMER >= timeout)
// becomes true, the action completes with the body's status. If the predicate
// fires first, <body> is aborted and <handler> runs — the handler's
// ABORT / THROW / RETURN determines the outcome (see AbortAction).

struct TryActionTemplate : public ActionTemplate {
    TryActionTemplate(Predicate *pred, MachineCommandTemplate *body, MachineCommandTemplate *handler);
    ~TryActionTemplate() override;
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;

    Condition condition;
    MachineCommandTemplate *body;
    MachineCommandTemplate *handler;
};

struct TryAction : public Action {
    TryAction(MachineInstance *mi, TryActionTemplate *t);
    ~TryAction() override;
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;

    Condition condition;
    MachineCommand *body;
    MachineCommand *handler;
    bool handler_started = false;
};
