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

#ifndef __SortListAction_H__
#define __SortListAction_H__ 1

#include <iostream>
#include "Action.h"
#include "symboltable.h"

class MachineInstance;

struct SortListActionTemplate : public ActionTemplate {
    
    // the bitset property named in the list_machine parameter are used to set the state of
    // entries in the list named in the dest parameter
    // The names are to be evaluated in the
    // scope where the command is used.
    
	SortListActionTemplate(Value list_machine, Value property_name);
    ~SortListActionTemplate();
    
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
       return out << "SortListAction "  << list_machine_name << " from " << property_name << "\n";
    }

    std::string list_machine_name;
    std::string property_name;
};

struct SortListAction : public Action {
	SortListAction(MachineInstance *m, const SortListActionTemplate *dat);
    SortListAction();
	Status run();
	Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;

	Value list_machine_name;
    Value property_name;
    MachineInstance *list_machine;
};

#endif
