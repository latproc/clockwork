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
#include "symboltable.h"

class MachineInstance;
class ExportState;

struct SendMessageActionTemplate : public ActionTemplate {
    SendMessageActionTemplate(Value msg, Value dest);
    SendMessageActionTemplate(Value msg, MachineInstance *dest);
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;
    void toC(std::ostream &out, std::ostream &vars) const override;
    Value message;
    Value target;
    MachineInstance *target_machine;
};

struct SendMessageAction : public Action {
    SendMessageAction(MachineInstance *mi, SendMessageActionTemplate &eat);
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;
    Value message;
    Value target;
    MachineInstance *target_machine;
};
