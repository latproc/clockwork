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
#include "symboltable.h"

struct ExpressionActionTemplate : public ActionTemplate {
    enum opType { opInc, opDec, opSet };
    ExpressionActionTemplate(CStringHolder var, opType op);
    ExpressionActionTemplate(CStringHolder var, opType op, const Value &v);
    ExpressionActionTemplate(CStringHolder var, opType op, const Value &v, const Value &e);
    ExpressionActionTemplate(CStringHolder var, opType op, int a);
    ExpressionActionTemplate(CStringHolder var, opType op, const char *a);
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;
    void toC(std::ostream &out, std::ostream &vars) const override;

    CStringHolder lhs;
    Value rhs;
    opType op;
    Value extra;
    Predicate *expr = nullptr;
};

class MachineInstance;
class Action;

struct ExpressionAction : public Action {
    ExpressionAction(MachineInstance *mi, ExpressionActionTemplate &eat);
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;

    CStringHolder lhs;
    Value rhs;
    ExpressionActionTemplate::opType op;
    Value extra;
    Predicate *expr = nullptr;
    MachineInstance *machine = nullptr;
};
