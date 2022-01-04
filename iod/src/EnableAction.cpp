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

#include "EnableAction.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include <algorithm>

EnableActionTemplate::EnableActionTemplate(const std::string &name, const char *property,
                                           const Value *val)
    : property_name(0), property_value(0) {
    machine_name.push_back(name);
    if (property && val) {
        property_name = strdup(property);
        property_value = strdup(val->asString().c_str());
    }
}

EnableActionTemplate::EnableActionTemplate(std::list<std::string> &names, const char *property,
                                           const Value *val)
    : property_name(0), property_value(0) {
    std::copy(names.begin(), names.end(), std::back_inserter(machine_name));
    if (property && val) {
        property_name = strdup(property);
        property_value = strdup(val->asString().c_str());
    }
}

EnableActionTemplate::~EnableActionTemplate() {
    free(property_name);
    free(property_value);
}

std::ostream &EnableActionTemplate::operator<<(std::ostream &out) const {
    out << "EnableAction template ";
    typename std::list<std::string>::const_iterator iter = machine_name.begin();
    while (iter != machine_name.end()) {
        out << *iter++;
        if (iter != machine_name.end()) {
            out << ", ";
        }
    }
    return out;
}

Action *EnableActionTemplate::factory(MachineInstance *mi) { return new EnableAction(mi, this); }

EnableAction::EnableAction(MachineInstance *m, const EnableActionTemplate *dat)
    : Action(m), property_name(0), property_value(0) {
    if (dat->property_name) {
        property_name = strdup(dat->property_name);
    }
    if (dat->property_value) {
        property_value = strdup(dat->property_value);
    }

    std::list<std::string>::const_iterator iter = dat->machine_name.begin();
    while (iter != dat->machine_name.end()) {
        machines.push_back(std::make_pair(*iter++, (MachineInstance *)0));
    }
}

EnableAction::EnableAction() {
    free(property_name);
    free(property_value);
}

std::ostream &EnableAction::operator<<(std::ostream &out) const {
    out << "EnableAction ";
    typename std::list<std::pair<std::string, MachineInstance *>>::const_iterator iter =
        machines.begin();
    while (iter != machines.end()) {
        out << (*iter++).first;
        if (iter != machines.end()) {
            out << ", ";
        }
    }
    return out;
}

Action::Status EnableAction::run() {
    owner->start(this);
    status = Complete; // assumption
    typename std::list<std::pair<std::string, MachineInstance *>>::iterator iter = machines.begin();
    while (iter != machines.end()) {
        std::pair<std::string, MachineInstance *> &item = *iter++;
        if (!item.second) {
            item.second = owner->lookup(item.first);
        }
        if (item.second) {
            item.second->enable();
        }
        else {
            char buf[150];
            snprintf(buf, 150, "EnableAction Error: cannot find %s from %s", item.first.c_str(),
                     owner->getName().c_str());
            MessageLog::instance()->add(buf);
            error_str = (const char *)buf;
            status = Failed;
        }
    }
    if (status == Complete || status == Failed) {
        owner->stop(this);
    }
    return status;
}

Action::Status EnableAction::checkComplete() {
    if (status == Complete || status == Failed) {
        return status;
    }
    if (this != owner->executingCommand()) {
        DBG_MSG << "checking complete on " << *this << " when it is not the top of stack \n";
    }
    else {
        status = Complete;
        owner->stop(this);
    }
    return status;
}
