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

#ifndef __DISABLEACTION_H__
#define __DISABLEACTION_H__ 1

#include <iostream>
#include <lib_clockwork_client/includes.hpp>
#include "Action.h"
// // #include "symboltable.h"

class MachineInstance;

struct DisableActionTemplate : public ActionTemplate {
	DisableActionTemplate(const std::string &name, const char *property = 0, const Value *val = 0);
    virtual ~DisableActionTemplate();
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
       return out << "DisableAction template "  << machine_name<< "\n";
    }

	std::string machine_name;
    char *property_name;
    char *property_value;
};

struct DisableAction : public Action {
	DisableAction(MachineInstance *m, const DisableActionTemplate *dat);
    virtual ~DisableAction();
	Status run();
	Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;

	std::string machine_name;
	MachineInstance *machine;
    char *property_name;
    char *property_value;
};

#endif
