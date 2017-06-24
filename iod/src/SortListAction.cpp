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

#include <algorithm>
#include <numeric>

#include "SortListAction.h"
#include "MachineInstance.h"
#include "Logger.h"

SortListActionTemplate::SortListActionTemplate(Value list_name, Value property)
: list_machine_name(list_name.asString()), property_name(property.asString()) {
}

SortListActionTemplate::~SortListActionTemplate() {
}
                                           
Action *SortListActionTemplate::factory(MachineInstance *mi) {
	return new SortListAction(mi, this);
}

SortListAction::SortListAction(MachineInstance *m, const SortListActionTemplate *dat)
    : Action(m), list_machine_name(dat->list_machine_name), property_name(dat->property_name), list_machine(0) {
}

SortListAction::SortListAction() : list_machine(0) {
}

std::ostream &SortListAction::operator<<(std::ostream &out) const {
	return out << "Sort List Action " << list_machine->getName() << " BY PROPERTY " << property_name;
}

class StringValueSorter {
public:
    bool operator()(const Parameter &a, const Parameter &b) const {
        return a.val.asString() < b.val.asString();
    }
};

class NumericValueSorter {
public:
    bool operator()(const Parameter &a, const Parameter &b) const {
        if (a.val.kind != Value::t_integer || b.val.kind != Value::t_integer) return true;
        long a_i, b_i;
        if (!a.val.asInteger(a_i) || !b.val.asInteger(b_i)) return true;
        return a_i < b_i;
    }
};

class PropertyValueStringSorter {
public:
    PropertyValueStringSorter(std::string property_name) : property(property_name) { }
    bool operator()(const Parameter &a, const Parameter &b) const {
        if (!a.machine) return true;
        if (!b.machine) return false;
        return a.machine->getValue(property) < b.machine->getValue(property);
    }
private:
    std::string property;
};

class PropertyValueNumericSorter {
public:
    PropertyValueNumericSorter(std::string property_name) : property(property_name) { }
    bool operator()(const Parameter &a, const Parameter &b) const {
        if (!a.machine) return true;
        if (!b.machine) return false;
        long a_val, b_val;
        if (!a.machine->getValue(property).asInteger(a_val)) return true;
        if (!b.machine->getValue(property).asInteger(b_val)) return false;
        return a_val < b_val;
    }
private:
    std::string property;
};

Action::Status SortListAction::run() {
	owner->start(this);
    list_machine = owner->lookup(list_machine_name);
	if (list_machine && list_machine->_type == "LIST") {
        
        if (list_machine->parameters.empty())
            status = Complete;
        else if (list_machine->parameters.at(0).val.kind == Value::t_symbol) {
            
            bool use_integer_sort = true;
            
            long val;
            // if the given property of any item in the list is not an integer, use a string sort
            for (unsigned int i=0; i<list_machine->parameters.size(); ++i) {
                MachineInstance *entry = list_machine->parameters[i].machine;
                if (entry && !entry->getValue(property_name.asString()).asInteger(val)) {
                    use_integer_sort = false;
                    break;
                }
            }
            if (use_integer_sort) {
                PropertyValueNumericSorter property_sort(property_name.asString());
                std::sort(list_machine->parameters.begin(), list_machine->parameters.end(), property_sort);
            }
            else {
                PropertyValueStringSorter property_sort(property_name.asString());
                std::sort(list_machine->parameters.begin(), list_machine->parameters.end(), property_sort);
            }
        }
        else {
            bool use_integer_sort = true;
            
            // if any item in the list is not an integer, use a string sort
            for (unsigned int i=0; i<list_machine->parameters.size(); ++i) {
                if (list_machine->parameters.at(i).val.kind != Value::t_integer) {
                    use_integer_sort = false;
                    break;
                }
            }
            if (use_integer_sort) {
                NumericValueSorter numeric_sort;
                std::sort(list_machine->parameters.begin(), list_machine->parameters.end(), numeric_sort);
            }
            else {
                StringValueSorter string_sort;
                std::sort(list_machine->parameters.begin(), list_machine->parameters.end(), string_sort);
            }
        }
        
        status = Complete;
	}
    else
        status = Failed;
    owner->stop(this);
	return status;
}

Action::Status SortListAction::checkComplete() {
	if (status == Complete || status == Failed) return status;
	if (this != owner->executingCommand()) {
		DBG_MSG << "checking complete on " << *this << " when it is not the top of stack \n";
	}
	else {
		status = Complete;
		owner->stop(this);
	}
	return status;
}

