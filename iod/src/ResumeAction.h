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
#include <iostream>

class MachineInstance;

struct ResumeActionTemplate : public ActionTemplate {
    ResumeActionTemplate(const std::string &name, const std::string &state,
                         const char *property = nullptr, const Value *val = nullptr);
    ~ResumeActionTemplate() override;
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override {
        return out << "ResumeAction template " << machine_name << "\n";
    }

    std::string machine_name;
    std::string resume_state;
    const char *property_name;
    const char *property_value;
};

struct ResumeAction : public Action {
    ResumeAction(MachineInstance *m, const ResumeActionTemplate *dat);
    ~ResumeAction() override;
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;

    std::string machine_name;
    std::string resume_state;
    MachineInstance *machine;
    const char *property_name;
    const char *property_value;
};
