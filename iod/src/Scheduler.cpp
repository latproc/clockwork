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

#include "Scheduler.h"
#include "DebugExtra.h"
#include "MessagingInterface.h"
#include "ProcessingThread.h"
#include <cassert>
#include <boost/chrono.hpp>
#include <iostream>
#include <zmq.hpp>
#include <iomanip>

// TODO: consider moving from boost to the std library, use
// std::condition_variable and wait on a notification for a time rather
// than the boost interruptible wait. Notifying the condition:
// { std::unique_lock<std::mutex> lock(m);
//   cond.wait_for(lock, std::chrono::seconds(5)); }
// .. cond.notify_all()

// The scheduler accesses two queues, one for scheduled items and one that provides triggered items
// to the processing thread. The other feeds triggered items and packages to the processing thread.
// Both queues need to be thread safe.

Scheduler *Scheduler::instance_ = nullptr;
extern bool program_done;

namespace {
    uint64_t calculate_delivery_time(long delay) {
        if (delay < 0) {
            DBG_SCHEDULER << "***** negative delta: " << delay << "\n";
            return microsecs();
        }
        uint64_t res = microsecs() + delay;
        return res;
    }

    void wait_for_start_message() {
        zmq::socket_t sync(*MessagingInterface::getContext(), ZMQ_REP);
        sync.bind("inproc://scheduler_sync");
        char buf[10];
        size_t response_len = 0;
        while (!safeRecv(sync, buf, 10, true, response_len, 0)) {
            boost::this_thread::sleep_for(boost::chrono::microseconds(100));
        }
    }

}

bool CompareSheduledItems::operator()(const ScheduledItem *a, const ScheduledItem *b) const {
    bool result = (*a) >= (*b);
    return result;
}

class SchedulerInternals {
public:
    SchedulerInternals() : thread_ptr(nullptr) {}
    boost::recursive_mutex q_mutex;
    boost::thread *thread_ptr;
};

bool ScheduledItem::operator<(const ScheduledItem &other) const {
    return delivery_time < other.delivery_time;
}

bool ScheduledItem::operator>=(const ScheduledItem &other) const {
    return delivery_time >= other.delivery_time;
}

ScheduledItem::ScheduledItem(long delay, Package *p) : package(p), action(0), trigger(0) {
    delivery_time = calculate_delivery_time(delay);
    DBG_SCHEDULER << "new scheduled package: " << delivery_time << "\n";
}

ScheduledItem::ScheduledItem(long delay, Action *a) : package(0), action(a), trigger(0) {
    delivery_time = calculate_delivery_time(delay);
    DBG_SCHEDULER << "new scheduled action: " << delay << "(" << delivery_time << ")\n";
}

ScheduledItem::ScheduledItem(long delay, Trigger *t) : package(0), action(0), trigger(t->retain()) {
    delivery_time = calculate_delivery_time(delay);
    DBG_SCHEDULER << "new scheduled action: " << delay << "(" << delivery_time << ")\n";
}

ScheduledItem::ScheduledItem(uint64_t starting, long delay, Action *a)
    : package(0), action(a), trigger(0) {
    delivery_time = starting + delay;
    DBG_SCHEDULER << "new scheduled action: " << delay << "(" << delivery_time << ")\n";
}

ScheduledItem::ScheduledItem(uint64_t starting, long delay, Trigger *t)
    : package(0), action(0), trigger(t->retain()) {
    delivery_time = starting + delay;
    DBG_SCHEDULER << "new scheduled action: " << delay << "(" << delivery_time << ")\n";
}

ScheduledItem::~ScheduledItem() {
    if (trigger) {
        trigger->release();
    }
    delete package;
    trigger = 0;
}

int64_t ScheduledItem::time_remaining() const {
    return delivery_time - microsecs();
}

std::ostream &ScheduledItem::operator<<(std::ostream &out) const {
    uint64_t now = microsecs();
    int64_t delta = delivery_time - now;
    out << delivery_time << " (" << delta << ") ";
    if (package) {
        out << *package;
    }
    else if (action) {
        out << *action;
    }
    else if (trigger) {
        out << *trigger;
    }
    else {
        out << "empty ScheduledItem";
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const ScheduledItem &item) {
    return item.operator<<(out);
}

// Priority queue

ScheduledItem *PriorityQueue::top() const {
    boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
    return queue.front();
}
bool PriorityQueue::empty() const {
    boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
    return queue.empty();
}
void PriorityQueue::pop() {
    boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
    queue.pop_front();
}
size_t PriorityQueue::size() const {
    boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
    return queue.size();
}

void PriorityQueue::push(ScheduledItem *item) {
    boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
    std::list<ScheduledItem *>::iterator iter = queue.begin();
    while (iter != queue.end()) {
        ScheduledItem *queued = *iter;
        if (*item < *queued) {
            queue.insert(iter, item);
            return;
        }
        else {
            iter++;
        }
    }
    queue.push_back(item);
}

bool PriorityQueue::check() const {
    boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
    ScheduledItem *prev = 0;
    std::list<ScheduledItem *>::const_iterator iter = queue.begin();
    while (iter != queue.end()) {
        if (prev == 0) {
            prev = *iter++;
        }
        else {
            ScheduledItem *cur = *iter++;
            assert(*cur >= *prev);
        }
    }
    return true;
}

// Scheduler

Scheduler::Scheduler(SharedThreadSafeQueue<ScheduledItem*> &queue)
    : state(e_waiting), m_queue(queue) {
    internals = new SchedulerInternals;
}

void Scheduler::setThreadRef(boost::thread &ref) { internals->thread_ptr = &ref; }

ScheduledItem* Scheduler::next() const {
    boost::recursive_mutex::scoped_lock scoped_lock(internals->q_mutex);
    return items.empty() ? nullptr : items.top();
}

void Scheduler::pop() {
    boost::recursive_mutex::scoped_lock scoped_lock(internals->q_mutex);
    //items.check();
    items.pop();
}

std::string Scheduler::getStatus() {
    boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
    assert(scoped_lock.owns_lock());
    uint64_t now = microsecs();
    std::stringstream ss;
    ss << "state: " << state << ", is ready: " << ready(now)
       << " items: " << items.size() << ", now: " << (now - ProcessingThread::programStartTime()) << "\n"
       << "last notification: " << std::fixed << std::setprecision(6);
    std::list<ScheduledItem *>::const_iterator iter = items.queue.begin();
    while (iter != items.queue.end()) {
        ScheduledItem *item = *iter++;
        ss << *item << "\n";
    }
    ss << std::ends;
    return ss.str();
}

int64_t Scheduler::microsecs_until_first_item() const {
    ScheduledItem *top = next();
    return top ? top->time_remaining() : 100000L;
}

int64_t Scheduler::microsecs_until_first_item(uint64_t start) const {
    ScheduledItem *top = next();
    return top ? top->delivery_time - start : 100000L;
}

// Add an item to the priority queue
// and check the current top
// and interrupt the scheduler thread if the top item is ready
void Scheduler::add(ScheduledItem *item) {
    ScheduledItem *top = nullptr;
    {
        boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
        DBG_SCHEDULER << "At " << microsecs() << ", scheduling item: " << *item << "\n";
        items.push(item);
    }
    int64_t delay = microsecs_until_first_item();
    if (internals->thread_ptr) {
        internals->thread_ptr->interrupt();
    }
}

bool Scheduler::ready(uint64_t start) {
    boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
    if (items.empty()) {
        return false;
    }
    return microsecs_until_first_item(start) <= 0;
}

void Scheduler::operator()() {
#ifdef __APPLE__
    pthread_setname_np("iod scheduler");
#else
    pthread_setname_np(pthread_self(), "iod scheduler");
#endif
    idle();
}

void Scheduler::stop() {
    state = e_aborted;
    //zmq::socket_t update_notify(*MessagingInterface::getContext(), ZMQ_PUSH);
    //update_notify.connect("inproc://sch_items");
    //safeSend(update_notify,"poke",4);
}

void Scheduler::idle() {
    wait_for_start_message();
    DBG_INITIALISATION << "Scheduler starting\n";

    while (state != e_aborted && !program_done) {
        long delay = microsecs_until_first_item();
        if (delay > 0) {
            try {
                {
                    boost::recursive_mutex::scoped_lock scoped_lock(
                       Scheduler::instance()->internals->q_mutex);
                    auto next = items.top();
                    if (!next) continue;
                    DBG_SCHEDULER << "Scheduler waiting " << delay << " for next item: " << *next << "\n";
                }
                auto start_time = microsecs();
                boost::this_thread::sleep_for(boost::chrono::microseconds(delay));
                auto end_time = microsecs();
                DBG_SCHEDULER << "Scheduler woke up after " << (end_time - start_time) << " us\n";
            }
            catch (const boost::thread_interrupted &) {
                // permit interruption
            }
        }
        delay = microsecs_until_first_item();
        while (delay <= 0) {
            ScheduledItem *item = nullptr;
            {
                boost::recursive_mutex::scoped_lock scoped_lock(
                    Scheduler::instance()->internals->q_mutex);
                item = next();
                if (!item) { continue; }
                DBG_SCHEDULER << "Scheduler at " << microsecs()
                    << "activating scheduled item " << (*item) << " ready. "
                    << items.size() << " items remain\n";
                pop();
            }
            if (item->trigger) {
                if (item->trigger->enabled()) {
                    m_queue.enqueue(item);
                }
                else {
                    delete item;
                }
            }
            else if (item->package) {
                m_queue.enqueue(item);
            }
            else if (item->action) {
                m_queue.enqueue(item);
            }
            else {
                assert(0 == "Scheduler could not process item");
                delete item;
            }
            delay = microsecs_until_first_item();
        }
    }
}
