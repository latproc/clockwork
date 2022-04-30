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

#ifndef __ClearListAction_H__
#define __ClearListAction_H__ 1

#include "Action.h"
#include "symboltable.h"
#include <iostream>

class MachineInstance;

struct ClearListActionTemplate : public ActionTemplate {

    // the bitset property named in the source parameter are used to set the state of
    // entries in the list named in the dest parameter
    // The names are to be evaluated in the
    // scope where the command is used.

    explicit ClearListActionTemplate(Value destination);
    ~ClearListActionTemplate() override;

    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override {
        return out << "Clear List or Reference " << dest_name << "\n";
    }

    std::string dest_name;
};

struct ClearListAction : public Action {
    ClearListAction(MachineInstance *m, const ClearListActionTemplate *dat);
    ClearListAction();
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;

    Value dest;
    MachineInstance *dest_machine;
};

#endif
