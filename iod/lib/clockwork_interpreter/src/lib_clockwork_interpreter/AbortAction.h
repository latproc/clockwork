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

#ifndef __ABORTACTION_H__
#define __ABORTACTION_H__ 1

#include <iostream>
#include <lib_clockwork_client/includes.hpp>

#include "Action.h"

class MachineInstance;

// Abort is a special action that is used within a Command list to stop further execution

struct AbortActionTemplate : public ActionTemplate {
	AbortActionTemplate(bool failed = true);
	AbortActionTemplate(bool failed, Value exception_message);
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
       return out << "Abort(failure=" << abort_fail <<") action template\n";
    }
	bool abort_fail;
	std::string message;
};

struct AbortAction : public Action {
	AbortAction(MachineInstance *m, const AbortActionTemplate *dat);
	Status run();
	Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;
	bool abort_fail;
	std::string message;
};

#endif
