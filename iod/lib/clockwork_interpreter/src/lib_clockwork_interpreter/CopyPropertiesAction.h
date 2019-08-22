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

#ifndef __CopyPropertiesAction_H__
#define __CopyPropertiesAction_H__ 1

#include <iostream>
#include <lib_clockwork_client/includes.hpp>
#include "Action.h"
// // #include "symboltable.h"

class MachineInstance;

struct CopyPropertiesActionTemplate : public ActionTemplate {

    // the machine named by the 'val' parameter is included in the list
    // given by the 'name' parameter.  The names are to be evaluated in the
    // scope where the command is used.

	CopyPropertiesActionTemplate(Value source, Value destination);
	CopyPropertiesActionTemplate(Value source, Value destination, const std::list<std::string>&properties);
    ~CopyPropertiesActionTemplate();

    virtual Action *factory(MachineInstance *mi);
	std::ostream &operator<<(std::ostream &out) const;

    std::string source_name;
    std::string dest_name;
	std::list<std::string> property_list;
};

struct CopyPropertiesAction : public Action {
	CopyPropertiesAction(MachineInstance *m, const CopyPropertiesActionTemplate *dat);
    CopyPropertiesAction();
	Status run();
	Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;

	Value source;
    Value dest;
    MachineInstance *source_machine;
    MachineInstance *dest_machine;
	std::list<std::string> property_list;
};

#endif
