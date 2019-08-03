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
#include <pthread.h>
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
#include "Channel.h"
#include <pthread.h>
#include "ProcessingThread.h"
#include "SharedWorkSet.h"

Dispatcher *Dispatcher::instance_ = NULL;
static boost::mutex dispatcher_mutex;
static boost::condition_variable package_available;
//boost::mutex Dispatcher::delivery_mutex;

// class SocketPool {
// public:
//     zmq::socket_t *get() {
//         std::thread::id current_thread_id = std::this_thread::get_id();
//         thread_map::iterator found = thread_map.find(current_thread_id);
//         if (found == thread_map.end()) {

//         }
//     }
// private:
//     typedef std::map<std::thread::id, zmq::socket_t*> thread_map;
//     thread_map sockets;
// }

void DispatchThread::operator()()
{
#ifdef __APPLE__
    pthread_setname_np("iod dispatcher");
#else
    pthread_setname_np(pthread_self(), "iod dispatcher");
#endif

    Dispatcher::instance()->idle();
}

Dispatcher::Dispatcher() : socket(0), started(false), dispatch_thread(0), thread_ref(0),
    sync(*MessagingInterface::getContext(), ZMQ_REP), status(e_waiting_cw),
	dispatch_socket(0), owner_thread(0)
{
    dispatch_thread = new DispatchThread;
    thread_ref = new boost::thread(boost::ref(*dispatch_thread));
}

Dispatcher::~Dispatcher()
{
    if (socket) delete socket;
	if (dispatch_socket) delete dispatch_socket;
}

Dispatcher *Dispatcher::instance()
{
    if (!instance_)
        instance_ = new Dispatcher();
    assert(instance_);
    return instance_;
}
void Dispatcher::start()
{
    while (!Dispatcher::instance()->started) usleep(20);
}

void Dispatcher::stop()
{
    if (status == e_running) sync.send("done", 4);

    instance()->status = e_aborted;
}

std::ostream &Dispatcher::operator<<(std::ostream &out) const
{
    out << "Dispatcher";
    return out;
}

std::ostream &operator<<(std::ostream &out, const Dispatcher &m)
{
    return m.operator<<(out);
}

void Dispatcher::addReceiver(Receiver*r)
{
    all_receivers.push_back(r);
}

void Dispatcher::removeReceiver(Receiver*r)
{
    all_receivers.remove(r);
}

void Dispatcher::deliver(Package *p)
{
    {
        boost::lock_guard<boost::mutex> lock(dispatcher_mutex);
        DBG_DISPATCHER << "Dispatcher accepted package " << *p << "\n";
        to_deliver.push_back(p);
    }
    package_available.notify_one();
}

void Dispatcher::idle()
{
	// this sync socket is used to request access to shared resources from
	// the driver
    sync.bind("inproc://dispatcher_sync");

	// the clockwork driver calls our start() method and that blocks until
	// we get to this point. Note that this thread will then
	// block until it gets a sync-start from the driver.
    started = true;
	DBG_MSG << "Dispatcher started\n";

	char buf[11];
	size_t response_len = 0;
	safeRecv(sync, buf, 10, true, response_len, 0); // wait for an ok to start from cw
	buf[response_len]= 0;
	NB_MSG << "Dispatcher got sync start: " << buf << "\n";

	/* this module waits for a start from clockwork and then starts looking for input on its
		command socket and its message socket (e_waiting). When either a command or message is detected
		it requests time from clockwork (e_waiting_cw). Clockwork in responds and the module
		reads the incoming request and processes it (e_running)
	 */

    status = e_waiting;
    while (status != e_aborted)
    {
		if (status == e_waiting) {
            boost::unique_lock<boost::mutex> lock(dispatcher_mutex);
            if (to_deliver.empty()) 
                package_available.wait(lock);
			status = e_waiting_cw;
		}
        if (status == e_waiting_cw)
        {
			sync.send("dispatch",8);
            safeRecv(sync, buf, 10, true, response_len, 0);
            status = e_running;
        }
        else if (status == e_running)
        {
            boost::lock_guard<boost::mutex> lock(dispatcher_mutex);
            while (!to_deliver.empty())
            {
                zmq::message_t reply;

                Package *p = to_deliver.front();
                to_deliver.pop_front();
                {
                    DBG_DISPATCHER << "Dispatcher sending package " << *p << "\n";
                    Receiver *to = p->receiver;
                    Transmitter *from = p->transmitter;
                    Message m(p->message); //TBD is this copy necessary
                    if (to)
                    {
                        MachineInstance *mi = dynamic_cast<MachineInstance*>(to);
                        Channel *chn = dynamic_cast<Channel*>(to);
											if (!mi->getStateMachine()) {
												char buf[100];
												snprintf(buf, 100, "Warning: Machine %s does not have a valid state machine", mi->getName().c_str());
												MessageLog::instance()->add(buf);
												NB_MSG << buf << "\n";
											}
                        if (!chn && mi && mi->getStateMachine() && mi->getStateMachine()->token_id == ClockworkToken::EXTERNAL)
                        {
                            DBG_DISPATCHER << "Dispatcher sending external message " << *p << " to " << to->getName() <<  "\n";
                            {
                                // The machine has no parameters take the properties from the machine
                                MachineInstance *remote = mi;
                                if (mi->parameters.size() > 0)
                                {
                                    remote = mi->lookup(mi->parameters[0]);
                                    // the host and port properties are specifed by the first parameter
                                    char buf[100];
                                    snprintf(buf, 100, "Error dispatching message,  EXTERNAL configuration: %s not found", mi->parameters[0].val.sValue.c_str());
                                    MessageLog::instance()->add(buf);
                                    NB_MSG << buf << "\n";
                                }
                                Value host = remote->properties.lookup("HOST");
                                Value port_val = remote->properties.lookup("PORT");
                                Value protocol = mi->properties.lookup("PROTOCOL");
                                long port;
                                if (port_val.asInteger(port))
                                {
                                    if (protocol == "RAW")
                                    {
                                           MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port, eRAW);
                                           if (!mif->started()) mif->start();
                                           mif->send_raw(m.getText().c_str());
                                    }
                                    else
                                    {
                                        if (protocol == "CLOCKWORK")
                                        {
                                            MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port, eCLOCKWORK);
                                            if (!mif->started()) mif->start();
                                            mif->send(m);
                                        }
                                        else
                                        {
                                            MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port, eZMQ);
																					  mif->start();
                                            mif->send(m.getText().c_str());
                                        }
                                    }
                                }
                            }
                        }
                        else if ( chn )
                        {
                            // when sending to a channel, if the channel has a publisher, get it to send the message
                            MessagingInterface *mif = chn->getPublisher();
                            if (mif)
                            {
                                Value protocol = mi->properties.lookup("PROTOCOL");
                                if (protocol == "RAW")
                                {
                                    mif->send_raw(m.getText().c_str());
                                }
                                else
                                {
                                    if (protocol == "CLOCKWORK")
                                    {
                                        mif->send(m);
                                    }
                                    else
                                    {
                                        mif->send(m.getText().c_str());
                                    }
                                }
                            }
                        }
                        else
                        {
                            DBG_DISPATCHER << "Dispatcher queued " << *p << " to " << to->getName() <<  "\n";
                            to->enqueue(*p);
                            //MachineInstance::forceIdleCheck();
                            MachineInstance *mi = dynamic_cast<MachineInstance*>(to);
                            if (mi)
                            {
                                SharedWorkSet::instance()->add(mi);
                                ProcessingThread::activate(mi);
                                Action *curr = mi->executingCommand();
                                if (curr)
                                {
                                    DBG_DISPATCHER << mi->getName() << " currently executing " << *curr << "\n";
                                }
                            }
                        }
                    }
                    else
                    {
						std::cout << "Warning: sending " << m << " to all receivers\n";
                        ReceiverList::iterator iter = all_receivers.begin();
                        while (iter != all_receivers.end())
                        {
                            Receiver *r = *iter++;
                            if (r->receives(m, from)) {
                                r->enqueue(*p);
                                //MachineInstance::forceIdleCheck();
                            }
                        }
                    }
                    delete p;
                }
            }
            sync.send("done", 4);
			safeRecv(sync, buf, 10, true, response_len, 0); // wait for ack from cw
			DBG_DISPATCHER << "Dispatcher done\n";
            status = e_waiting;
        }
    }
}
