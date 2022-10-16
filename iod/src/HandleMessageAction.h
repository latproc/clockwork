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
#include "symboltable.h"

class MachineInstance;

struct HandleMessageActionTemplate : public ActionTemplate {
    explicit HandleMessageActionTemplate(const Package &p) : package(p) {}
    HandleMessageActionTemplate(Transmitter *t, Receiver *r, const Message &m, bool needs_receipt)
        : package(t, r, m, needs_receipt) {}
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override {
        return out << "Message: " << package.message;
    }
    Package package;
};

struct HandleMessageAction : public Action {
    HandleMessageAction(MachineInstance *mi, HandleMessageActionTemplate &hmt)
        : Action(mi), package(hmt.package), machine(0), handler(0) {}
    ~HandleMessageAction() override;
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;
    Package package;
    MachineInstance *machine;
    Action *handler;
};
