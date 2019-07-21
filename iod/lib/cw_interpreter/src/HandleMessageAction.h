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

#ifndef __HANDLE_MESSAGE_ACTION
#define __HANDLE_MESSAGE_ACTION 1

#include <iostream>
#include "Action.h"
#include "symboltable.h"

class MachineInstance;

struct HandleMessageActionTemplate : public ActionTemplate {
    HandleMessageActionTemplate(const Package &p) : package(p) { }
    HandleMessageActionTemplate(Transmitter *t, Receiver *r, const Message &m, bool needs_receipt) 
		: package(t, r, new Message(m), needs_receipt) { }
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
		return out << "Message: " << package.message;
    }
    Package package;
};

struct HandleMessageAction : public Action {
    HandleMessageAction(MachineInstance *mi, HandleMessageActionTemplate &hmt)  : Action(mi), package(hmt.package), machine(0), handler(0) {}
    ~HandleMessageAction();
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;
    Package package;
    MachineInstance *machine;
	Action *handler;
};


#endif
