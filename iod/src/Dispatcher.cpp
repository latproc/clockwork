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

#include "Dispatcher.h"
#include "Channel.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "Message.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "ProcessingThread.h"
#include "SharedWorkSet.h"
#include "symboltable.h"
#include <assert.h>
#include <functional>
#include <iostream>
#include <list>
#include <pthread.h>
#include <zmq.hpp>

namespace {
struct Cleanup {
    std::function<void()> cleanup;
    Cleanup(std::function<void()> &&cleanup) : cleanup(std::move(cleanup)) {}
    ~Cleanup() { cleanup(); }
};
} // namespace

Dispatcher *Dispatcher::instance_ = nullptr;
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

void DispatchThread::operator()() {
#ifdef __APPLE__
    pthread_setname_np("iod dispatcher");
#else
    pthread_setname_np(pthread_self(), "iod dispatcher");
#endif

    Dispatcher::instance()->idle();
}

Dispatcher::Dispatcher(SharedThreadSafeQueue<Package*> &q)
    : started(false), finished(false), dispatch_thread(0), thread_ref(0),
      owner_thread(0),
      process_queue(q), command_queue( q.get_cond_var_any(), q.get_cond_var_mutex()),
      to_deliver(q.get_cond_var_any(), q.get_cond_var_mutex()){}

Dispatcher::~Dispatcher() {
    if (!instance()->finished) {
        stop();
    }
    // if (socket) { delete socket; }
    join();
    instance_ = nullptr;
}

Dispatcher *Dispatcher::create(SharedThreadSafeQueue<Package*> &q) {
    if (!instance_) { instance_ = new Dispatcher(q); }
    else if (&instance()->process_queue != &q) {
        assert("dispatcher created with a different queue" && &instance()->process_queue == &q);
    }
    return instance_;
}

Dispatcher *Dispatcher::instance() {
    assert("calling instance before create" && instance_);
    return instance_;
}

void Dispatcher::start() {
    auto dispatcher = Dispatcher::instance();
    if (dispatcher->dispatch_thread) {
        return;
    }
    dispatcher->dispatch_thread = new DispatchThread;
    dispatcher->thread_ref = new boost::thread(boost::ref(*dispatcher->dispatch_thread));
}

void Dispatcher::sync_start() {
    command_queue.enqueue("start");
}

void Dispatcher::reset() {
    finished = false;
    started = false;
}

void Dispatcher::join() {
    if (thread_ref) {
        thread_ref->join();
    }
    if (thread_ref) {
        delete thread_ref;
        thread_ref = nullptr;
    }
    if (dispatch_thread) {
        delete dispatch_thread;
        dispatch_thread = nullptr;
    }
}

void Dispatcher::stop() {
    instance()->finished = true;
    package_available.notify_one();
    command_queue.enqueue("exit");
    join();
}

std::ostream &Dispatcher::operator<<(std::ostream &out) const {
    out << "Dispatcher";
    return out;
}

std::ostream &operator<<(std::ostream &out, const Dispatcher &m) { return m.operator<<(out); }

void Dispatcher::addReceiver(Receiver *r) { all_receivers.push_back(r); }

void Dispatcher::removeReceiver(Receiver *r) { all_receivers.remove(r); }

void Dispatcher::deliver(Package *p) {
    {
        boost::lock_guard<boost::mutex> lock(dispatcher_mutex);
        DBG_DISPATCHER << "Dispatcher accepted package " << *p << "\n";
        to_deliver.push_back(p);
    }
    package_available.notify_one();
}

void Dispatcher::pumpToProcessQueue() {
    Package *p = 0;
    while (to_deliver.try_pop_front(p)) {
        if (p) {
            process_queue.enqueue(p);
        }
    }
}

bool Dispatcher::wait() {
    if (finished || !to_deliver.is_empty() || !command_queue.is_empty()) {
        return true;
    }
    boost::shared_lock<boost::shared_mutex> lock(to_deliver.get_cond_var_mutex());
    to_deliver.get_cond_var_any().wait(lock, [this] {
        return finished || !to_deliver.is_empty() || !command_queue.is_empty();
    });
    return true;
}

void Dispatcher::idle() {
    std::string command_message;
    DBG_DISPATCHER << "------- Dispatcher waiting for start\n";
    while (!finished && !started) {
        wait();
        if (!command_queue.is_empty()) {
            if (command_queue.try_dequeue(command_message)) {
                if (command_message == "exit") { finished = true; }
                else if (command_message == "start") { started = true; }
                else { assert("unexpected command message" && false); }
            }
            else {
                assert("Dispatcher should have received a start message" && false);
            }
        }
    }

    DBG_DISPATCHER << "------ Dispatcher got sync start: " << command_message << "\n";

    /*  this module waits for a start from clockwork and then starts
     *  looking for input on its command socket and its message socket (e_waiting).
     */
    while (!finished) {
        wait();
        if (finished) {
            break;
        }
        if (!command_queue.is_empty()) {
            if (command_queue.try_dequeue(command_message)) {
                if (command_message == "exit") {
                    finished = true;
                }
            }
        }
        while (!to_deliver.is_empty()) {
            Package *p;
            if (to_deliver.try_pop_front(p)) {
                process_queue.enqueue(p);
            }
        }
    }
}
