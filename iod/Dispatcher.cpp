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

#include <iostream>
#include <list>
#include "Dispatcher.h"
#include "Message.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "MessagingInterface.h"
#include "symboltable.h"
#include <assert.h>

Dispatcher *Dispatcher::instance_ = NULL;

Dispatcher::Dispatcher() : socket(0) {
    socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PULL);
    socket->bind("inproc://dispatcher");
}

Dispatcher::~Dispatcher() {
    if (socket) delete socket;
}

Dispatcher *Dispatcher::instance() {
    if (!instance_) instance_ = new Dispatcher();
    assert(instance_);
    return instance_;
}

std::ostream &Dispatcher::operator<<(std::ostream &out) const  {
    out << "Dispatcher";
    return out;
}

std::ostream &operator<<(std::ostream &out, const Dispatcher &m) {
    return m.operator<<(out);
}

void Dispatcher::addReceiver(Receiver*r) {
    all_receivers.push_back(r);
}

void Dispatcher::removeReceiver(Receiver*r) {
    all_receivers.remove(r);
}
void Dispatcher::deliver(Package *p) {
    zmq::socket_t sender(*MessagingInterface::getContext(), ZMQ_PUSH);
    sender.connect("inproc://dispatcher");
    sender.send(&p, sizeof(Package*));
}
void Dispatcher::deliverZ(Package *p) {
	DBG_DISPATCHER << "Dispatcher accepted package " << *p << "\n";
    to_deliver.push_back(p);
}

void Dispatcher::idle() {
    // TBD this is being converted to use zmq the local delivery queue is no longer necessary
    
	std::list<Package*>local_to_deliver;
	// copy the queue to a local list before delivery
    while (1) {
        Package *p = 0;
        size_t len = socket->recv(&p, sizeof(Package*), ZMQ_NOBLOCK);
        if (len)local_to_deliver.push_back(p); else break;
    }
    std::list<Package*>::iterator iter = to_deliver.begin();
	// handle the current group of messages
	iter = local_to_deliver.begin();
    while (iter != local_to_deliver.end()) {
        Package *p = *iter;
		DBG_DISPATCHER << "Dispatcher sending package " << *p << "\n";
        Receiver *to = p->receiver;
        Transmitter *from = p->transmitter;
        Message m(p->message); //TBD is this copy necessary
        if (to) {
            MachineInstance *mi = dynamic_cast<MachineInstance*>(to);
            if (mi && mi->getStateMachine()->token_id == ClockworkToken::EXTERNAL) {
                DBG_DISPATCHER << "Dispatcher sending external message " << *p << " to " << to->getName() <<  "\n";
                {
                    // The machine no parameters take the properties from the machine
                    MachineInstance *remote = mi;
                    if (mi->parameters.size() > 0) {
                        remote = mi->lookup(mi->parameters[0]);
                         // the host and port properties are specifed by the first parameter
                        char buf[100];
                        snprintf(buf, 100, "Error dispatching message,  EXTERNAL configuration: %s not found", mi->parameters[0].val.sValue.c_str());
                        MessageLog::instance()->add(buf);
                    }
                    Value host = remote->properties.lookup("HOST");
                    Value port_val = remote->properties.lookup("PORT");
                    Value protocol = mi->properties.lookup("PROTOCOL");
                    long port;
                    if (port_val.asInteger(port)) {
                        if (protocol == "RAW") {
                        	MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port, eRAW);
                            mif->send_raw(m.getText().c_str());
						}
                        else {
                            if (protocol == "CLOCKWORK") {
                        		MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port, eCLOCKWORK);
                                mif->send(m);
							}
                            else {
                        		MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port, eZMQ);
								mif->send(m.getText().c_str());
							}
                        }
                    }
                }
            }
            else {
                DBG_DISPATCHER << "Dispatcher queued " << *p << " to " << to->getName() <<  "\n";
                to->enqueue(*p);
                MachineInstance *mi = dynamic_cast<MachineInstance*>(to);
				if (mi) {
					Action *curr = mi->executingCommand();
					if (curr) {
						DBG_DISPATCHER << mi->getName() << " currently executing " << *curr << "\n";
					}
				}
            }
        } else {
            // since acting on a message may cause new machines to 
            // be generated, and thus alter the reciever list,
            // we first make a list of machines to receive the 
            // message then actually send it.
            ReceiverList to_receive;
            ReceiverList::iterator iter = all_receivers.begin();
            while (iter != all_receivers.end()) {
                Receiver *r = *iter++;
                if (r->receives(m, from)) 
					to_receive.push_back(r);
				else
					DBG_DISPATCHER << r->getName() << " will not accept package " << *p << "\n";
            }
            iter = to_receive.begin();
            while (iter != to_receive.end()) {
                Receiver *r = *iter++;
				//Package pkg(from, r, new Message(m));
				r->enqueue(*p);
//                r->handle(m, from);
            }

        }
        delete p;
        iter = local_to_deliver.erase(iter);
    }
}

