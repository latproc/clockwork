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

#include "Action.h"
#include "Expression.h"
#include "Message.h"
#include "MessagingInterface.h"
#include "State.h"
#include "symboltable.h"
#include <zmq.hpp>

class MachineInstance;

struct HandleRequestActionTemplate : public ActionTemplate {
    HandleRequestActionTemplate(zmq::socket_t *, const char *) {}
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override { return out; }
    zmq::socket_t *sock;
    std::string request;
};

struct HandleRequestAction : public Action {
    HandleRequestAction(MachineInstance *mi, HandleRequestActionTemplate &t)
        : Action(mi), sock(t.sock), request(t.request) {}
    Status run() override;
    Status checkComplete() override;
    void setAuthority(uint64_t auth) { authority = auth; }
    std::ostream &operator<<(std::ostream &out) const override;

  protected:
    uint64_t authority = 0;
    zmq::socket_t *sock = nullptr;
    std::string request;
    MessageHeader header;
};
