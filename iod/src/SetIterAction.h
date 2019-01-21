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

#ifndef __SetIterAction_H__
#define __SetIterAction_H__ 1

#include <iostream>
#include "Action.h"
#include "symboltable.h"

class MachineInstance;

struct SetIterActionTemplate : public ActionTemplate {
    
    // the machine named by the 'val' parameter is included in the list
    // given by the 'name' parameter.  The names are to be evaluated in the
    // scope where the command is used.
    
	SetIterActionTemplate(const std::string &name, Value val, Value pos = -1, bool before = false, bool expand = false);
    ~SetIterActionTemplate();
    
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
       return out << "SetIterAction template "  << list_machine_name << " " << entry << "\n";
    }

	std::string list_machine_name; // name of the list to include the machine into
	Value entry;
	Value position;
	bool before;
	bool expand;
};

struct SetIterAction : public Action {
	SetIterAction(MachineInstance *m, const SetIterActionTemplate *dat, const Value pos, bool before, bool expand = false);
	SetIterAction();
	Status run();
	Status checkComplete();
	virtual std::ostream &operator<<(std::ostream &out)const;

	std::string list_machine_name;
	Value entry;
	MachineInstance *list_machine;
	MachineInstance *entry_machine;
	Value position;
	bool before;
	bool expand;
};

#endif
