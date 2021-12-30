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

#ifndef __PopListAction_H__
#define __PopListAction_H__ 1

#include "Action.h"
#include "symboltable.h"
#include <iostream>

class MachineInstance;

struct PopBackActionTemplate : public ActionTemplate {

    // the bitset property named in the list_machine parameter are used to set the state of
    // entries in the list named in the dest parameter
    // The names are to be evaluated in the
    // scope where the command is used.

    PopBackActionTemplate(Value list_machine);
    ~PopBackActionTemplate();

    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "PopBackAction " << list_machine_name << "\n";
    }

    std::string list_machine_name;
};

struct PopBackAction : public Action {
    PopBackAction(MachineInstance *m, const PopBackActionTemplate *dat);
    PopBackAction();
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;

    Value list_machine_name;
    MachineInstance *list_machine;
};

struct PopFrontActionTemplate : public ActionTemplate {

    // the bitset property named in the list_machine parameter are used to set the state of
    // entries in the list named in the dest parameter
    // The names are to be evaluated in the
    // scope where the command is used.

    PopFrontActionTemplate(Value list_machine);
    ~PopFrontActionTemplate();

    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "PopFrontAction " << list_machine_name << "\n";
    }

    std::string list_machine_name;
};

struct PopFrontAction : public Action {
    PopFrontAction(MachineInstance *m, const PopFrontActionTemplate *dat);
    PopFrontAction();
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;

    Value list_machine_name;
    MachineInstance *list_machine;
};

struct GetListItemActionTemplate : public ActionTemplate {

    // the bitset property named in the list_machine parameter are used to set the state of
    // entries in the list named in the dest parameter
    // The names are to be evaluated in the
    // scope where the command is used.

    GetListItemActionTemplate(Value list_machine, Value property_name);
    ~GetListItemActionTemplate();

    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "GetListItemAction " << property_name << " from " << list_machine_name
                   << "\n";
    }

    std::string list_machine_name;
    std::string property_name;
};

struct GetListItemAction : public Action {
    GetListItemAction(MachineInstance *m, const GetListItemActionTemplate *dat);
    GetListItemAction();
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;

    Value list_machine_name;
    Value property_name;
    MachineInstance *list_machine;
};

#endif
