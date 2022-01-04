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

#ifndef __SetOperationAction_H__
#define __SetOperationAction_H__ 1

#include "Action.h"
#include "Expression.h"
#include "symboltable.h"
#include <iostream>

class MachineInstance;

enum SetOperation { soIntersect, soUnion, soDifference, soSelect };

struct SetOperationActionTemplate : public ActionTemplate {

    // the bitset property named in the source parameter are used to set the state of
    // entries in the list named in the dest parameter
    // The names are to be evaluated in the
    // scope where the command is used.

    SetOperationActionTemplate(Value num, Value a, Value b, Value destination, Value property,
                               SetOperation op, Predicate *pred, bool remove, Value start = -1,
                               Value end = -1);
    ~SetOperationActionTemplate();

    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "SetOperationAction " << src_a_name << ", src_b_name "
                   << " to " << dest_name << " using " << property_name << "\n";
    }

    Value count;
    std::string src_a_name;
    std::string src_b_name;
    std::string dest_name;
    std::string property_name;
    SetOperation operation;
    Condition condition;
    bool remove_selected;
    Value start_pos;
    Value end_pos;
};

struct SetOperationAction : public Action {
    SetOperationAction(MachineInstance *m, const SetOperationActionTemplate *dat);
    SetOperationAction();
    Status run();
    Status checkComplete();
    virtual Status doOperation();
    virtual std::ostream &operator<<(std::ostream &out) const;

  protected:
    MachineInstance *scope;
    Value count;
    Value source_a;
    Value source_b;
    Value dest;
    Condition condition;
    std::string property_name;
    MachineInstance *source_a_machine;
    MachineInstance *source_b_machine;
    MachineInstance *dest_machine;
    SetOperation operation;
    bool remove_selected;
    Value start_pos;
    Value end_pos;
    long sp;
    long ep;
};

struct IntersectSetOperation : public SetOperationAction {
    IntersectSetOperation(MachineInstance *m, const SetOperationActionTemplate *dat);
    Status doOperation();
};

struct UnionSetOperation : public SetOperationAction {
    UnionSetOperation(MachineInstance *m, const SetOperationActionTemplate *dat);
    Status doOperation();
};

struct DifferenceSetOperation : public SetOperationAction {
    DifferenceSetOperation(MachineInstance *m, const SetOperationActionTemplate *dat);
    Status doOperation();
};

struct SelectSetOperation : public SetOperationAction {
    SelectSetOperation(MachineInstance *m, const SetOperationActionTemplate *dat);
    Status doOperation();
};

#endif
