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
}

void Dispatcher::removeReceiver(Receiver*r) {
    all_receivers.remove(r);
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
			r->enqueue(*p);
			//r->mail_queue.push_back(p);
		}
	}
	delete p->message;
	delete p;
#endif
}

struct MessageSender {
	MessageSender(const char *remote, std::string message) : done(false), aborted(false), url(remote), msg(message) { }
	void operator()() {
		zmq::socket_t sock(*MessagingInterface::getContext(), ZMQ_REQ);
		sock.connect(url.c_str());
		while (1) {
			try {
				sock.send(msg.c_str(), msg.length());
				break;
			}
			catch(zmq::error_t io) { std::cout << "zmq error sending message to " << url << ": " << zmq_strerror(errno) << "\n"; }
			catch(std::exception ex) { std::cout << "error sending " << url << ": " << zmq_strerror(errno) << "\n"; }
		}
		while (1) {
			try {
				char buf[100];
				int len = sock.recv(buf, 99);
				if (len>=0) { buf[len] = 0; if (strncmp(buf, "OK", len) != 0) { std::cout << " got " << buf <<" when sending to " << url << "\n"; } }
				break;
			}
			catch(zmq::error_t io) { std::cout << "zmq error sending message to " << url << ": " << zmq_strerror(errno) << "\n"; }
			catch(std::exception ex) { std::cout << "error sending " << url << ": " << zmq_strerror(errno) << "\n"; }
		}
		done = true;
		while (!aborted) usleep(50);
	}
	void stop() { aborted = true; }
	bool done;
	bool aborted;
	std::string url;
	std::string msg;
};
std::map<boost::thread*, MessageSender*> senders;
std::list<boost::thread*> threads;


void Dispatcher::idle() {
	// copy the queue to a local list before delivery
	std::list<Package*>local_to_deliver;
    std::list<Package*>::iterator iter = to_deliver.begin();
    while (iter != to_deliver.end()) {
        Package *p = *iter;
		iter = to_deliver.erase(iter);
		local_to_deliver.push_back(p);
	}


	// cleanup old threads;
	std::list<boost::thread*>::iterator thread_iter = threads.begin();
	while (thread_iter != threads.end()) { 
		boost::thread *t = *thread_iter;
		MessageSender *m = (*senders.find(t)).second;
		if (m->done) {
			m->stop();
			t->join();
			thread_iter = threads.erase(thread_iter);
			delete t;
			delete m;
		}
		else thread_iter++;
	}	

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
                        else if (protocol == "CLOCKWORK") {
                        		MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port, eCLOCKWORK);
                            mif->send(m);
												}
                       	else if (protocol == "ACKED") {
													char url[100];
													snprintf(url, 100, "tcp://%s:%d", host.asString().c_str(), (int)port);
													MessageSender *sender = new MessageSender(url, m.getText());
													boost::thread *m_sender = new boost::thread(boost::ref(*sender));
													senders[m_sender] = sender;
													threads.push_back(m_sender);
												}
                        else {
                        	MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port, eZMQ);
													mif->send(m.getText().c_str());
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

