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

struct SetListEntriesActionTemplate : public ActionTemplate {

    // the bitset property named in the source parameter are used to set the state of
    // entries in the list named in the dest parameter
    // The names are to be evaluated in the
    // scope where the command is used.

    SetListEntriesActionTemplate(Value source, Value destination);
    ~SetListEntriesActionTemplate() override;

    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override {
        return out << "SetListEntriesAction " << source_name << " from " << dest_name << "\n";
    }

    std::string source_name;
    std::string dest_name;
};

class SetListEntriesAction : public Action {
  public:
    SetListEntriesAction(MachineInstance *m, const SetListEntriesActionTemplate *dat);
    SetListEntriesAction();
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;

    Value source;
    Value dest;
    MachineInstance *dest_machine;

  private:
    void setListEntries(unsigned long val);
};
