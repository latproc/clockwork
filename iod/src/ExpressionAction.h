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

#ifndef __EXPRESSIONACTION_H__
#define __EXPRESSIONACTION_H__ 1

#include "Action.h"
#include "Expression.h"
#include "symboltable.h"
#include <iostream>

class MachineInstance;
class Action;

struct ExpressionActionTemplate : public ActionTemplate {
    enum opType { opInc, opDec, opSet };
    ExpressionActionTemplate(CStringHolder var, opType oper) : lhs(var), rhs(1), op(oper) {
        if (op == opDec) {
            rhs = -1;
        }
    }
    ExpressionActionTemplate(CStringHolder var, opType oper, const Value &v)
        : lhs(var), rhs(v), op(oper) {
        if (op == opDec) {
            rhs = -v;
        }
    }
    ExpressionActionTemplate(CStringHolder var, opType oper, const Value &v, const Value &e)
        : lhs(var), rhs(v), op(oper), extra(e) {
        if (op == opDec) {
            rhs = -v;
        }
    }
    ExpressionActionTemplate(CStringHolder var, opType oper, int a) : lhs(var), rhs(a), op(oper) {
        if (op == opDec) {
            rhs = -a;
        }
        else {
            rhs = a;
        }
    }
    ExpressionActionTemplate(CStringHolder var, opType oper, const char *a)
        : lhs(var), rhs(a), op(oper) {
        assert(op == opSet);
    }
    ExpressionActionTemplate(CStringHolder var, Predicate *predicate)
        : lhs(var), op(opSet), expr(predicate) {}

    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        switch (op) {
        case opInc:
            out << "INC ";
            break;
        case opDec:
            out << "DEC ";
            break;
        case opSet:
            out << "SET ";
            break;
        }
        return out << lhs.get() << " " << rhs << " "
                   << "\n";
    }
    CStringHolder lhs;
    Value rhs;
    opType op;
    Value extra;
    Predicate *expr;
};

struct ExpressionAction : public Action {
    ExpressionAction(MachineInstance *mi, ExpressionActionTemplate &eat);
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;
    CStringHolder lhs;
    Value rhs;
    ExpressionActionTemplate::opType op;
    Value extra;
    Predicate *expr = nullptr;
    MachineInstance *machine = nullptr;
};

#endif
