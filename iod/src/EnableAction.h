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
#include <list>

class MachineInstance;

struct EnableActionTemplate : public ActionTemplate {
    explicit EnableActionTemplate(const std::string &name, const char *property = nullptr,
                                  const Value *val = nullptr);
    EnableActionTemplate(std::list<std::string> &names, const char *property = nullptr,
                         const Value *val = nullptr);
    ~EnableActionTemplate() override;
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;

    std::list<std::string> machine_name;
    char *property_name;
    char *property_value;
};

struct EnableAction : public Action {
    EnableAction(MachineInstance *m, const EnableActionTemplate *dat);
    EnableAction();
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;

    std::list<std::pair<std::string, MachineInstance *>> machines;
    //std::string machine_name;
    //MachineInstance *machine;
    char *property_name;
    char *property_value;
};
