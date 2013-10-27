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

#ifndef cwlang_Scheduler_h
#define cwlang_Scheduler_h

#include <ostream>
#include <string>
#include <list>
#include <sys/time.h>

#include "Action.h"
#include "Message.h"
#include <boost/thread.hpp>

struct ScheduledItem {
	Package *package;
	Action *action;
	struct timeval delivery_time;
	// this operator produces a reverse ordering because the standard priority queue is a max value queue.
	bool operator<(const ScheduledItem& other) const;
	ScheduledItem(long when, Package *p);
	ScheduledItem(long when, Action *a);
    std::ostream &operator <<(std::ostream &out) const;
};

std::ostream &operator <<(std::ostream &out, const ScheduledItem &item);

class PriorityQueue {
public:
	PriorityQueue() {}
	void push(ScheduledItem *item);
	ScheduledItem* top() const { return queue.front(); }
	bool empty() const { return queue.empty(); }
	void pop() { queue.pop_front(); }
	size_t size() const { return queue.size(); }

	std::list<ScheduledItem*> queue;

protected:	
	PriorityQueue(const PriorityQueue& other);
	PriorityQueue &operator=(const PriorityQueue& other);
};

class Scheduler {
public:
	static Scheduler *instance() { if (!instance_) instance_ = new Scheduler(); return instance_; }
    std::ostream &operator<<(std::ostream &out) const;
	void add(ScheduledItem*);
	ScheduledItem *next() const { if (items.empty()) return 0; else return items.top(); }
	void pop() { items.pop(); }
	bool ready();
    void idle();
	bool empty() { return items.empty(); }
    int clear(const Transmitter *transmitter, const Receiver *receiver, const char *message);
    
protected:
    Scheduler();
	~Scheduler() {}
	static Scheduler *instance_;
	PriorityQueue items;
	struct timeval next_time;
};

std::ostream &operator<<(std::ostream &out, const Scheduler &m);

#endif
