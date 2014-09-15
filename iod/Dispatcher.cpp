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
#include "Channel.h"
#include <pthread.h>

Dispatcher *Dispatcher::instance_ = NULL;

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
    sync(*MessagingInterface::getContext(), ZMQ_REP), status(e_waiting_cw)
{
    dispatch_thread = new DispatchThread;
    thread_ref = new boost::thread(boost::ref(*dispatch_thread));
}

Dispatcher::~Dispatcher()
{
    if (socket) delete socket;
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
    zmq::socket_t sender(*MessagingInterface::getContext(), ZMQ_PUSH);
    sender.connect("inproc://dispatcher");
    sender.send(&p, sizeof(Package*));
}

void Dispatcher::deliverZ(Package *p)
{
    DBG_DISPATCHER << "Dispatcher accepted package " << *p << "\n";
    to_deliver.push_back(p);
}

void Dispatcher::idle()
{
    socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PULL);
    socket->bind("inproc://dispatcher");

	// this sync socket is used to request access to shared resources from
	// the driver
    sync.bind("inproc://dispatcher_sync");

    // TBD this is being converted to use zmq the local delivery queue is no longer necessary
    zmq::socket_t command(*MessagingInterface::getContext(), ZMQ_REP);
    command.bind("inproc://dispatcher_cmd");

	// the clockwork driver calls our start() method and that blocks until
	// we get to this point. Note that this thread will then 
	// block until it gets a sync-start from the driver.
    started = true;
	std::cout << "Dispatcher started\n";
	
	char buf[11];
	size_t response_len = 0;
	safeRecv(sync, buf, 10, true, response_len); // wait for an ok to start from cw
	buf[response_len]= 0;
	std::cout << "Dispatcher got sync start: " << buf << "\n";
	
    /*
    { // wait for a start command
    char cmd[10];
    size_t cmdlen = command.recv(cmd, 10);
    while (cmdlen == 0) {
        cmdlen = command.recv(cmd, 10);
    }
     */


	/* this module waits for a start from clockwork and then starts looking for input on its
		command socket and its message socket (e_waiting). When either a command or message is detected
		it requests time from clockwork (e_waiting_cw). Clockwork in responds and the module 
		reads the incoming request and processes it (e_running)
	 */

    status = e_waiting;
    zmq::pollitem_t items[] = {  
		{ *socket, 0, ZMQ_POLLIN, 0 }, 
		{ command, 0, ZMQ_POLLIN, 0 },
	};
    while (status != e_aborted)
    {
		if (status == e_waiting) {
            // check for messages to be sent or commands to be processed (TBD)
            int rc = zmq::poll( &items[0], 2, -1);
            if (rc== -1) {
				if ( errno == EAGAIN) continue;
				char msgbuf[200];
				snprintf(msgbuf,200, "Dispatcher poll error: %s", strerror(errno));
				MessageLog::instance()->add(msgbuf);
			}
			status = e_waiting_cw;
		}
        if (status == e_waiting_cw)
        {
#if 0
            try     // wait for sync signal from clockwork
            {
                char sync_buf[10];
                size_t sync_len = 0;
                zmq::pollitem_t items[] = {  { sync, 0, ZMQ_POLLIN, 0 },};
                int rc = zmq::poll( &items[0], 1, 500);
                if (!rc) continue;
                if (items[0].revents & ZMQ_POLLIN)
                {
                    sync_len = sync.recv(sync_buf, 10);
                    if (sync_len == 0 && errno == EAGAIN) continue;
                }
                else continue;
            }
            catch (zmq::error_t e)
            {
                if (errno == EINTR) continue;
                std::cerr << "Dispatch: " << zmq_strerror(errno) << "\n";
                usleep(100000);
                break;
            }
#else
			//std::cout << "Dispatcher waiting to work\n";
			sync.send("dispatch",8);
            safeRecv(sync, buf, 10, true, response_len);
#endif
            status = e_running;
        }
        else if (status == e_running)
        {
			//std::cout << "Dispatcher running\n";
            while (items[0].revents & ZMQ_POLLIN)
            {
                zmq::message_t reply;

                Package *p = 0;
                size_t len = socket->recv(&p, sizeof(Package*), ZMQ_DONTWAIT);
                if (len)
                {
                    DBG_DISPATCHER << "Dispatcher sending package " << *p << "\n";
                    Receiver *to = p->receiver;
                    Transmitter *from = p->transmitter;
                    Message m(p->message); //TBD is this copy necessary
                    if (to)
                    {
                        MachineInstance *mi = dynamic_cast<MachineInstance*>(to);
                        Channel *chn = dynamic_cast<Channel*>(to);
                        if (!chn && mi && mi->getStateMachine()->token_id == ClockworkToken::EXTERNAL)
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
                                        mif->send_raw(m.getText().c_str());
                                    }
                                    else
                                    {
                                        if (protocol == "CLOCKWORK")
                                        {
                                            MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port, eCLOCKWORK);
                                            mif->send(m);
                                        }
                                        else
                                        {
                                            MessagingInterface *mif = MessagingInterface::create(host.asString(), (int) port, eZMQ);
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
                            MachineInstance *mi = dynamic_cast<MachineInstance*>(to);
                            if (mi)
                            {
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
                        ReceiverList::iterator iter = all_receivers.begin();
                        while (iter != all_receivers.end())
                        {
                            Receiver *r = *iter++;
                            if (r->receives(m, from))
                                r->enqueue(*p);
                        }
                    }
                    delete p;
                }
                zmq::pollitem_t skt_poll = { *socket, 0, ZMQ_POLLIN, 0 };
                items[0] = skt_poll;
                zmq::poll( &items[0], 1, 0); // check for more incoming messages
            }
            if (items[1].revents & ZMQ_POLLIN)
            {
                std::cout << "dispatcher command\n";
            }
            sync.send("done", 4);
			safeRecv(sync, buf, 10, true, response_len); // wait for ack from cw
			//std::cout << "Dispatcher done\n";
            status = e_waiting;
        }
    }
}

