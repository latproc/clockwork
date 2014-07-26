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
#include <zmq.hpp>

Dispatcher *Dispatcher::instance_ = NULL;

void DispatchThread::operator()() {
    Dispatcher::instance()->idle();
}

Dispatcher::Dispatcher() : socket(0), started(false), thread_ref(boost::ref(dispatch_thread)) {
}

Dispatcher::~Dispatcher() {
    if (socket) delete socket;
}

Dispatcher *Dispatcher::instance() {
    if (!instance_) instance_ = new Dispatcher();
    assert(instance_);
    return instance_;
}
void Dispatcher::start() {
    while (!Dispatcher::instance()->started) usleep(20);
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
    socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PULL);
    socket->bind("inproc://dispatcher");
    
    zmq::socket_t sync(*MessagingInterface::getContext(), ZMQ_REP);
    sync.bind("inproc://dispatcher_sync");
    started = true;

    // TBD this is being converted to use zmq the local delivery queue is no longer necessary
    zmq::socket_t command(*MessagingInterface::getContext(), ZMQ_REP);
    command.bind("inproc://dispatcher_cmd");
    started = true;
    /*
    { // wait for a start command
    char cmd[10];
    size_t cmdlen = command.recv(cmd, 10);
    while (cmdlen == 0) {
        cmdlen = command.recv(cmd, 10);
    }
     */

    enum {e_waiting, e_running} status = e_waiting;
    while (1) {
        if (status == e_waiting) try {   // wait for sync signal from clockwork
            char sync_buf[10];
            size_t sync_len = 0;
            zmq::pollitem_t items[] = {  { sync, 0, ZMQ_POLLIN, 0 },};
            int rc = zmq::poll( &items[0], 1, 500);
            if (!rc) continue;
            if (items[0].revents & ZMQ_POLLIN) {
                sync_len = sync.recv(sync_buf, 10);
                if (sync_len == 0 && errno == EAGAIN) continue;
            }
            else continue;
        }
        catch (zmq::error_t e) {
            std::cerr << "Dispatch: " << zmq_strerror(errno) << "\n";
            continue;
        }
        status = e_running;
        
        // check for messages to be sent or commands to be processed (TBD)
        zmq::pollitem_t items[] = {  { *socket, 0, ZMQ_POLLIN, 0 }, { command, 0, ZMQ_POLLIN, 0 } };
        int rc = zmq::poll( &items[0], 2, 0);
        if (rc== -1 && errno == EAGAIN) continue;
        
        while (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t reply;

            Package *p = 0;
            size_t len = socket->recv(&p, sizeof(Package*), ZMQ_NOBLOCK);
            if (len) {
                DBG_DISPATCHER << "Dispatcher sending package " << *p << "\n";
                Receiver *to = p->receiver;
                Transmitter *from = p->transmitter;
                Message m(p->message); //TBD is this copy necessary
                if (to) {
                    MachineInstance *mi = dynamic_cast<MachineInstance*>(to);
                    if (mi && mi->getStateMachine()->token_id == ClockworkToken::EXTERNAL) {
                        DBG_DISPATCHER << "Dispatcher sending external message " << *p << " to " << to->getName() <<  "\n";
                        {
                            // The machine has no parameters take the properties from the machine
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
                    /*
                     
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
                        r->enqueue(*p);
                    }
                     */
                    ReceiverList::iterator iter = all_receivers.begin();
                    while (iter != all_receivers.end()) {
                        Receiver *r = *iter++;
                        if (r->receives(m, from))
                            r->enqueue(*p);
                    }
                }
                delete p;
//                len = socket->recv(&p, sizeof(Package*), ZMQ_NOBLOCK);
            }
            items[0] = { *socket, 0, ZMQ_POLLIN, 0 };
            zmq::poll( &items[0], 1, 0);
        }
        if (items[1].revents & ZMQ_POLLIN) {
            std::cout << "dispatcher command\n";
        }
        sync.send("done", 4);
        status = e_waiting;
    }
}

