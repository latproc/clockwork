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
#include <iostream>

class MachineCommandTemplate;
class MachineCommand;
class MachineInstance;

struct IfCommandActionTemplate : public ActionTemplate {
    ~IfCommandActionTemplate() override = default;
    IfCommandActionTemplate(Predicate *pred, MachineCommandTemplate *mct)
        : condition(pred), command(mct) {}
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;

    Condition condition;
    MachineCommandTemplate *command;
};

struct IfCommandAction : public Action {
    IfCommandAction(MachineInstance *mi, IfCommandActionTemplate *t);
    ~IfCommandAction() override;
    Status run() override;
    Status checkComplete() override;
    virtual std::ostream &operator<<(std::ostream &out) const override;

    Condition condition;
    MachineCommand *command;
};

struct IfElseCommandActionTemplate : public ActionTemplate {
    IfElseCommandActionTemplate(Predicate *pred, MachineCommandTemplate *mct,
                                MachineCommandTemplate *else_clause)
        : condition(pred), command(mct), else_command(else_clause) {}
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;

    Condition condition;
    MachineCommandTemplate *command;
    MachineCommandTemplate *else_command;
};

struct IfElseCommandAction : public Action {
    IfElseCommandAction(MachineInstance *mi, IfElseCommandActionTemplate *t);
    ~IfElseCommandAction() override;
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;

    Condition condition;
    MachineCommand *command;
    MachineCommand *else_command;
};
