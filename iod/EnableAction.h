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

#ifndef __ENABLEACTION_H__
#define __ENABLEACTION_H__ 1

#include <iostream>
#include "Action.h"
#include "symboltable.h"

class MachineInstance;

struct EnableActionTemplate : public ActionTemplate {
	EnableActionTemplate(const std::string &name, const char *property = 0, const Value *val = 0);
    ~EnableActionTemplate();
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
       return out << "EnableAction template "  << machine_name<< "\n";
    }

	std::string machine_name;
    const char *property_name;
    const char *property_value;
};

struct EnableAction : public Action {
	EnableAction(MachineInstance *m, const EnableActionTemplate *dat);
    EnableAction();
	Status run();
	Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;

	std::string machine_name;
	MachineInstance *machine;
    const char *property_name;
    const char *property_value;
};

#endif
