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
#include "Action.h"

class MachineInstance;

struct DisableActionTemplate : public ActionTemplate {
	DisableActionTemplate(const std::string &name) : machine_name(name){ }
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
       return out << "DisableAction template "  << machine_name<< "\n";
    }

	std::string machine_name;
};

struct DisableAction : public Action {
	DisableAction(MachineInstance *m, const DisableActionTemplate *dat) : Action(m), machine_name(dat->machine_name), machine(0) { }
	Status run();
	Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;

	std::string machine_name;
	MachineInstance *machine;
};

#endif
