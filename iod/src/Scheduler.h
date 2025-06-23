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
    /Users/martin/projects/latproc/github/latproc/iod/IfCommandAction.h
    You should have received a copy of the GNU General Public License
    along with Latproc; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#pragma once

#include <boost/thread/thread.hpp>

#include <list>
#include <ostream>
#include <string>
#include <sys/time.h>

#include "Action.h"
#include "Message.h"
#include <zmq.hpp>
#include <ThreadSafeQueue.h>

struct ScheduledItem {
    Package *package;
    Action *action;
    Trigger *trigger;
    uint64_t delivery_time;
    // this operator produces a reverse ordering because the standard priority queue is a max value queue.
    bool operator<(const ScheduledItem &other) const;
    bool operator>=(const ScheduledItem &other) const;
    ScheduledItem(long delay, Package *p);
    ScheduledItem(long delay, Action *a);
    ScheduledItem(long delay, Trigger *t);
    ScheduledItem(uint64_t starting, long delay, Action *a);
    ScheduledItem(uint64_t starting, long delay, Trigger *t);
    ~ScheduledItem();
    int64_t time_remaining() const;
    std::ostream &operator<<(std::ostream &out) const;

    friend std::ostream &operator<<(std::ostream &out, const ScheduledItem &item);

  private:
    bool operator<=(const ScheduledItem &other) const;
    bool operator>(const ScheduledItem &other) const;
    bool operator==(const ScheduledItem &other) const;
};


class PriorityQueue {
  public:
    PriorityQueue() {}
    void push(ScheduledItem *item);
    ScheduledItem *top() const;
    bool empty() const;
    void pop();
    size_t size() const;
    bool check() const;

    std::list<ScheduledItem *> queue;

  protected:
    PriorityQueue(const PriorityQueue &other);
    PriorityQueue &operator=(const PriorityQueue &other);
};

class CompareSheduledItems {
  public:
    bool operator()(const ScheduledItem *a, const ScheduledItem *b) const;
};

class SchedulerInternals;
class Scheduler {
  public:
    static Scheduler *instance() {
        assert(instance_);
        return instance_;
    }
    static Scheduler *create(SharedThreadSafeQueue<ScheduledItem*> & queue) {
        if (!instance_) {
            instance_ = new Scheduler(queue);
        }
        return instance_;
    }
    //std::ostream &operator<<(std::ostream &out) const;
    void add(ScheduledItem *);
    ScheduledItem *next() const;
    void pop();
    bool ready(uint64_t start);
    void idle();
    bool empty() const { return items.empty(); }
    int clear(const Transmitter *transmitter, const Receiver *receiver, const char *message);
    std::string getStatus();

    void operator()();
    void stop();
    void setThreadRef(boost::thread &ref);

  protected:
    SchedulerInternals *internals;
    Scheduler(SharedThreadSafeQueue<ScheduledItem*> &queue);
    ~Scheduler() {}
    static Scheduler *instance_;
    PriorityQueue items;

    enum State {
        e_waiting,
        e_aborted
    } state;

private:
    int64_t microsecs_until_first_item() const;
    int64_t microsecs_until_first_item(uint64_t start) const;
    SharedThreadSafeQueue<ScheduledItem*> &m_queue;

    friend class PriorityQueue;
};
