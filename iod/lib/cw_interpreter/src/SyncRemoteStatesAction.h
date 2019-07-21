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

#ifndef __SYNCREMOTESTATES_ACTION__
#define __SYNCREMOTESTATES_ACTION__ 1

#include <iostream>
#include "Action.h"
#include "symboltable.h"
#include "Message.h"
#include "State.h"
#include "Expression.h"
#include <zmq.hpp>

class Channel;
class MachineInstance;

struct SyncRemoteStatesActionTemplate : public ActionTemplate {
	SyncRemoteStatesActionTemplate(Channel *channel, zmq::socket_t *socket);
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "SyncRemoteStatesAction";
    }
    std::string trigger_event;
	Channel *chn;
	zmq::socket_t *sock;
};

class SyncRemoteStatesActionInternals;
class SyncRemoteStatesAction : public Action {
public:
	SyncRemoteStatesAction(MachineInstance *mi, SyncRemoteStatesActionTemplate &t);
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;

protected:
	Status execute();
	SyncRemoteStatesActionInternals *internals;
};

#endif
