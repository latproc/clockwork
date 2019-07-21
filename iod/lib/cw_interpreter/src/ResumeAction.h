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

#ifndef __ResumeACTION_H__
#define __RESUMEACTION_H__ 1

#include <iostream>
#include "Action.h"
#include "symboltable.h"

class MachineInstance;

struct ResumeActionTemplate : public ActionTemplate {
	ResumeActionTemplate(const std::string &name, const std::string &state, const char *property = 0, const Value *val = 0);
    ~ResumeActionTemplate();
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
       return out << "ResumeAction template "  << machine_name<< "\n";
    }

	std::string machine_name;
	std::string resume_state;
    const char *property_name;
    const char *property_value;
};

struct ResumeAction : public Action {
	ResumeAction(MachineInstance *m, const ResumeActionTemplate *dat);
    ~ResumeAction();
	Status run();
	Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;

	std::string machine_name;
	std::string resume_state;
	MachineInstance *machine;
    const char *property_name;
    const char *property_value;
};

#endif
