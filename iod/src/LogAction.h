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

#include "Action.h"
#include "Expression.h"
#include "symboltable.h"
#include <ostream>

class MachineInstance;

struct LogActionTemplate : public ActionTemplate {
    explicit LogActionTemplate(CStringHolder msg) : message(msg.get()), predicate(nullptr) {}
    explicit LogActionTemplate(const Value &v) : message(v), predicate(nullptr) {}
    explicit LogActionTemplate(Predicate *pred) : message(SymbolTable::Null), predicate(pred) {}
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;
    void toC(std::ostream &out, std::ostream &vars) const override;
    Value message;
    Predicate *predicate;
};

struct LogAction : public Action {
    LogAction(MachineInstance *mi, LogActionTemplate &lat)
        : Action(mi), message(lat.message), predicate(lat.predicate) {}
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;
    Value message;
    Predicate *predicate;
};
