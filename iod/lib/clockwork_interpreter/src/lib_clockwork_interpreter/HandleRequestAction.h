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

#ifndef __HANDLEREQUEST_ACTION__
#define __HANDLEREQUEST_ACTION__ 1

#include "Action.h"
#include <lib_clockwork_client/includes.hpp>
// // #include "Message.h"
#include "Expression.h"
#include "State.h"
// #include <zmq.hpp>
// // #include "MessagingInterface.h"

class MachineInstance;

struct HandleRequestActionTemplate : public ActionTemplate {
    HandleRequestActionTemplate(zmq::socket_t *s, const char *msg) {}
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const { return out; }
    zmq::socket_t *sock;
    std::string request;
};

struct HandleRequestAction : public Action {
    HandleRequestAction(MachineInstance *mi, HandleRequestActionTemplate &t)
        : Action(mi), sock(t.sock), request(t.request) {}
    Status run();
    Status checkComplete();
    void setAuthority(uint64_t auth) { authority = auth; }
    virtual std::ostream &operator<<(std::ostream &out) const;

  protected:
    uint64_t authority;
    zmq::socket_t *sock;
    std::string request;
    MessageHeader header;
};

#endif
