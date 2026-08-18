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

#pragma once

#include <boost/thread.hpp>
#include <list>
#include <map>
#include <ostream>
#include <string>
#include <utility>
#include "ThreadSafeQueue.h"
#include "Message.h"

class Message;
class Receiver;
struct Package;

class DispatchThread {
  public:
    void operator()();
};

class Dispatcher {
  public:
	using ReceiverList = ThreadSafeList<Receiver*>;
    ReceiverList all_receivers;

    ~Dispatcher();
    std::ostream &operator<<(std::ostream &out) const;
    void deliver(Package *p);
    void deliverZ(Package *p);
    // Move queued packages onto the processing queue (processing thread).
    void pumpToProcessQueue();
    void addReceiver(Receiver *r);
    void removeReceiver(Receiver *r);
    static Dispatcher *create(SharedThreadSafeQueue<Package*> &process_queue);
    static Dispatcher *instance();

    static void start();
    void idle();
    void stop();
    void reset();
    void join();
    void sync_start();

  private:
    Dispatcher(SharedThreadSafeQueue<Package*> &process_queue);
    Dispatcher(const Dispatcher &orig);
    Dispatcher &operator=(const Dispatcher &other);
    bool wait();
    static Dispatcher *instance_;
    bool started;
    bool finished;
    DispatchThread *dispatch_thread;
    boost::thread *thread_ref;
    enum {
        e_waiting,
        e_waiting_cw,
        w_waiting_cmd,
        e_running,
        e_aborted,
        e_handling_dispatch
    } status;
    pthread_t owner_thread;
    SharedThreadSafeQueue<Package*> &process_queue;
    SharedThreadSafeQueue<std::string> command_queue;
    SharedThreadSafeList<Package *> to_deliver;
};

std::ostream &operator<<(std::ostream &out, const Dispatcher &m);
