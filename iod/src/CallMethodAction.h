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

#ifndef __CALLMETHOD_ACTION
#define __CALLMETHOD_ACTION 1

#include "Action.h"
#include "symboltable.h"
#include <iostream>

class MachineInstance;

struct CallMethodActionTemplate : public ActionTemplate {
    CallMethodActionTemplate(CStringHolder msg, CStringHolder dest,
                             CStringHolder timeout = "CALL_TIMEOUT",
                             CStringHolder error = "CALL_ERROR");
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;
    CStringHolder message;
    CStringHolder target;
    CStringHolder timeout_symbol;
    CStringHolder error_symbol;
};

struct CallMethodAction : public Action {
    CallMethodAction(MachineInstance *mi, CallMethodActionTemplate &eat);
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;
    CStringHolder message;
    CStringHolder target;
    MachineInstance *target_machine;
};

#endif
