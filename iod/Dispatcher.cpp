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
#include "DebugExtra.h"
#include "Logger.h"
#include "MessagingInterface.h"
#include "symboltable.h"

Dispatcher *Dispatcher::instance_ = NULL;

Dispatcher::Dispatcher() {
    
}

Dispatcher *Dispatcher::instance() {
    if (!instance_) instance_ = new Dispatcher();
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
    std::cout << " added receiver " << r->getName() << "\n";
}

void Dispatcher::removeReceiver(Receiver*r) {
    all_receivers.remove(r);
    std::cout << " removed reciever " << r->getName() << "\n";
}
void Dispatcher::deliver(Package *p) {
#if 1
	DBG_DISPATCHER << "Dispatcher accepted package " << *p << "\n";
    to_deliver.push_back(p);
#else
//    std::cout << "Dispatcher accepted package with message " << (*p->message) << "\n";

	Message m(p->message->getText().c_str());
	Receiver *to = p->receiver;
	Transmitter *from = p->transmitter;
//        std::cout << "Dispatcher is attempting to deliver " 
//        << m << " from " << ( (from)?from->getName():"unknown") 
//        << " to " << ( (to) ? to->getName():"all") << "\n";
	if (to) {
		if (to->receives(m, from)) to->enqueue(*p);
		else
			DBG_DISPATCHER << to->getName() << " will not accept package " << *p << "\n";
	}
	else {
		// since acting on a message may cause new machines to 
		// be generated, and thus alter the reciever list,
		// we first make a list of machines to receive the 
		// message then actually send it.
		ReceiverList to_receive;
		ReceiverList::iterator iter = all_receivers.begin();
		while (iter != all_receivers.end()) {
			Receiver *r = *iter++;
			if (r->receives(m, from)) to_receive.push_back(r);
		}
		iter = to_receive.begin();
		while (iter != to_receive.end()) {
			Receiver *r = *iter++;
			//r->enqueue(*p);
			r->mail_queue.push_back(p);
		}
	}
	delete p->message;
	delete p;
#endif
}

void Dispatcher::idle() {
	// copy the queue to a local list before delivery
	std::list<Package*>local_to_deliver;
    std::list<Package*>::iterator iter = to_deliver.begin();
    while (iter != to_deliver.end()) {
        Package *p = *iter;
		iter = to_deliver.erase(iter);
		local_to_deliver.push_back(p);
	}
	// handle the current group of messages
	iter = local_to_deliver.begin();
    while (iter != local_to_deliver.end()) {
        Package *p = *iter;
		DBG_DISPATCHER << "Dispatcher sending package " << *p << "\n";
        Receiver *to = p->receiver;
        Transmitter *from = p->transmitter;
        Message m(p->message->getText().c_str());
        if (to) {
            MachineInstance *mi = dynamic_cast<MachineInstance*>(to);
            if (mi && mi->_type == "EXTERNAL") {
                DBG_DISPATCHER << "Dispatcher sending external message " << *p << " to " << to->getName() <<  "\n";
                {
                    Value host = mi->getValue("HOST");
                    Value port_val = mi->getValue("PORT");
                    long port;
                    if (port_val.asInteger(port)) {
                        MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port);
                        std::stringstream ss;
                        mif->send(m.getText().c_str());
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
					DBG_DISPATCHER << to->getName() << " will not accept package " << *p << "\n";
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

