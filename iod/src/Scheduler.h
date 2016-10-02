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

#ifndef cwlang_Scheduler_h
#define cwlang_Scheduler_h

#include <ostream>
#include <string>
#include <list>
#include <sys/time.h>
#include <queue>
#include <vector>

#include "Action.h"
#include "Message.h"
#include <boost/thread.hpp>
#include <zmq.hpp>

struct ScheduledItem {
	Package *package;
	Action *action;
	uint64_t delivery_time;
	// this operator produces a reverse ordering because the standard priority queue is a max value queue.
	bool operator<(const ScheduledItem& other) const;
	bool operator>=(const ScheduledItem& other) const;
	ScheduledItem(long delay, Package *p);
	ScheduledItem(long delay, Action *a);
	ScheduledItem(uint64_t starting, long delay, Action *a);
	~ScheduledItem();
    std::ostream &operator <<(std::ostream &out) const;

private:
	bool operator<=(const ScheduledItem& other) const;
	bool operator>(const ScheduledItem& other) const;
	bool operator==(const ScheduledItem& other) const;
};

std::ostream &operator <<(std::ostream &out, const ScheduledItem &item);


class PriorityQueue {
public:
	PriorityQueue() {}
	void push(ScheduledItem *item);
	ScheduledItem* top() const;
	bool empty() const;
	void pop();
	size_t size() const;
	bool check() const;

	std::list<ScheduledItem*> queue;

protected:	
	PriorityQueue(const PriorityQueue& other);
	PriorityQueue &operator=(const PriorityQueue& other);
};


class CompareSheduledItems {
public:
    bool operator()(const ScheduledItem *a, const ScheduledItem *b) const;
};

class SchedulerInternals;
class Scheduler {
public:
	static Scheduler *instance() { if (!instance_) instance_ = new Scheduler(); return instance_; }
    //std::ostream &operator<<(std::ostream &out) const;
	void add(ScheduledItem*);
	ScheduledItem *next() const;
	void pop();
	bool ready(uint64_t start);
    void idle();
	bool empty() { return items.empty(); }
    int clear(const Transmitter *transmitter, const Receiver *receiver, const char *message);
	std::string getStatus();

	void operator()();
	void stop();
	int64_t getNextDelay();
	int64_t getNextDelay(uint64_t start);

protected:
	SchedulerInternals *internals;
    Scheduler();
	~Scheduler() {}
	static Scheduler *instance_;
    //std::priority_queue<ScheduledItem*, std::vector<ScheduledItem*>, CompareSheduledItems> items;
    PriorityQueue items;
	uint64_t next_time;
	enum State { 
		e_waiting,  // waiting for something to do
		e_have_work, // new items have been pushed and need to be checked
		e_waiting_cw,  // waiting to begin processing
		e_running, 	// processing triggered events
		e_aborted 	// time to shutdown
	} state;
	zmq::socket_t update_sync;
	zmq::socket_t *update_notify;
	long next_delay_time;
	uint64_t notification_sent; // the scheduler has been notified that an item is scheduled

	friend class PriorityQueue;
};

//std::ostream &operator<<(std::ostream &out, const Scheduler &m);

#endif
