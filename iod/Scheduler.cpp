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
#include <iomanip>
#include "Logger.h"
#include "Scheduler.h"
#include "MachineInstance.h"
#include "DebugExtra.h"
#include "MessagingInterface.h"
#include "MessageLog.h"
#include <zmq.hpp>

Scheduler *Scheduler::instance_;
static const uint32_t ONEMILLION = 1000000;

static void setTime(struct timeval &now, unsigned long delta) {
	gettimeofday(&now, 0);
	DBG_SCHEDULER << "setTime: " << now.tv_sec << std::setfill('0')<< std::setw(6) << now.tv_usec << "\n";
	now.tv_sec += delta / ONEMILLION;
	now.tv_usec += delta % ONEMILLION;
	if (now.tv_usec >= ONEMILLION) {
		++now.tv_sec;
		now.tv_usec -= ONEMILLION;
	}
}

std::string Scheduler::getStatus() { 
	char buf[100];
	if (state == e_running) 
		snprintf(buf, 100, "busy");
	else
		snprintf(buf, 100, "state: %d, items: %ld", state, items.size());
	return buf;
}

bool ScheduledItem::operator<(const ScheduledItem& other) const {
    if (delivery_time.tv_sec < other.delivery_time.tv_sec) return true;
    if (delivery_time.tv_sec == other.delivery_time.tv_sec) return delivery_time.tv_usec < other.delivery_time.tv_usec;
    return false;
}

bool ScheduledItem::operator>=(const ScheduledItem& other) const {
    if (delivery_time.tv_sec > other.delivery_time.tv_sec) return true;
    if (delivery_time.tv_sec == other.delivery_time.tv_sec) return delivery_time.tv_usec >= other.delivery_time.tv_usec;
    return false;
}

void PriorityQueue::push(ScheduledItem *item) {
	std::list<ScheduledItem*>::iterator iter = queue.begin();
	while (iter!= queue.end()) {
		ScheduledItem *queued = *iter;
		if (*item < *queued) {
			queue.insert(iter, item);
			return;
		}
		else
			iter++;
	}
	queue.push_back(item);
}

ScheduledItem::ScheduledItem(long when, Package *p) :package(p), action(0) {
	setTime(delivery_time, when);
	DBG_SCHEDULER << "scheduled package: " << delivery_time.tv_sec << std::setfill('0')<< std::setw(6) << delivery_time.tv_usec << "\n";
}
ScheduledItem::ScheduledItem(long when, Action *a) :package(0), action(a) {
	setTime(delivery_time, when);
	DBG_SCHEDULER << "scheduled action: " << delivery_time.tv_sec << std::setfill('0')<< std::setw(6) << delivery_time.tv_usec << "\n";
}

std::ostream &ScheduledItem::operator <<(std::ostream &out) const {
    out << delivery_time.tv_sec << std::setfill('0')<< std::setw(6) << delivery_time.tv_usec;
    if (package) out << *package; else if (action) out << *action;
    return out;
}

std::ostream &operator <<(std::ostream &out, const ScheduledItem &item) {
    return item.operator<<(out);
}

Scheduler::Scheduler() : state(e_waiting), update_sync(*MessagingInterface::getContext(), ZMQ_PULL), next_delay_time(0) { 
	update_sync.bind("inproc://sch_items");
	next_time.tv_sec = 0;
	next_time.tv_usec = 0;
}

bool CompareSheduledItems::operator()(const ScheduledItem *a, const ScheduledItem *b) const {
    bool result = (*a) >= (*b);
    return result;
}

void Scheduler::add(ScheduledItem*item) {
    DBG_SCHEDULER << "Scheduling item: " << *item << "\n";
	//DBG_SCHEDULER << "Before schedule::add() " << *this << "\n";
	items.push(item);
	next_time = next()->delivery_time;
	// should update next_delay_time
	struct timeval now;
	gettimeofday(&now, 0);
    ScheduledItem *top = next();
    next_delay_time = get_diff_in_microsecs(&top->delivery_time, &now);
	//DBG_SCHEDULER << "After schedule::add() " << *this << "\n";
	if (!notification_sent) {
		notification_sent = true;
		zmq::socket_t update_notify(*MessagingInterface::getContext(), ZMQ_PUSH);
		int send_state = 0; // disconnected
		while (true) {
			try {
				if (send_state == 0) {
					update_notify.connect("inproc://sch_items");
					send_state = 1; // connected
				}
				safeSend(update_notify,"poke",4);
				break;
			}		
			catch(zmq::error_t err) {
				if (zmq_errno() == EINTR) continue;
				
				char errmsg[100];
				snprintf(errmsg, 100, "Scheduler::add error: %s", zmq_strerror( zmq_errno()));
				std::cerr << errmsg << "\n";
				MessageLog::instance()->add(errmsg);
				throw;
			} 
		}
	}
}

/*
std::ostream &Scheduler::operator<<(std::ostream &out) const  {
    std::list<ScheduledItem*>::const_iterator iter = items.begin();
	while (iter != items.queue.end()) {
        ScheduledItem *item = *iter++;
        struct timeval now;
        gettimeofday(&now, 0);
        long dt = get_diff_in_microsecs(&item->delivery_time, &now);
		out << "Scheduler: {" << item->delivery_time.tv_sec << "." << std::setfill('0')<< std::setw(6) << item->delivery_time.tv_usec
                << " (" << dt << "usec) }" << "\n  ";
		if (item->package) out << *(item->package);
		else if (item->action) out << *(item->action);
        break;
	}
    out << " (" << items.size() << " items)\n";
    return out;
}

std::ostream &operator<<(std::ostream &out, const Scheduler &m) {
    return m.operator<<(out);
}
 */

bool Scheduler::ready() {
	if (empty()) { next_delay_time = -1; return false; }
	//DBG_SCHEDULER<< "scheduler checking " << *this << "\n";
	if (next_time.tv_sec == 0) {
		next_time = next()->delivery_time;
	}
	struct timeval now;
	gettimeofday(&now, 0);
    ScheduledItem *top = next();
    next_delay_time = get_diff_in_microsecs(&top->delivery_time, &now);
	//if (now.tv_sec > next_time.tv_sec) return true;
	//if (now.tv_sec == next_time.tv_sec) return now.tv_usec >= next_time.tv_usec;
    if (next_delay_time<=0) return true;
	return false;
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
	zmq::socket_t update_notify(*MessagingInterface::getContext(), ZMQ_PUSH);
	update_notify.connect("inproc://sch_items");
	safeSend(update_notify,"poke",4);
}
	

void Scheduler::idle() {
	zmq::socket_t sync(*MessagingInterface::getContext(), ZMQ_REP);
	sync.bind("inproc://scheduler_sync");
	char buf[10];
	size_t response_len = 0;
	safeRecv(sync, buf, 10, true, response_len);
	std::cout << "Scheduler started\n";

	state = e_waiting;
	bool is_ready;
	while (state != e_aborted) {
		if (!ready() && state == e_waiting) {
			DBG_SCHEDULER << "scheduler waiting for work " << next_delay_time << "\n";
			long delay = next_delay_time;
			if (delay == -1) {
				assert(empty());
				safeRecv(update_sync, buf, 10, false, response_len, -1);
			}
			else if (delay < 1000)
				usleep((unsigned int)delay);
			else
				safeRecv(update_sync, buf, 10, false, response_len, delay/1000);
			//std::cout << "scheduler got some work\n";
			notification_sent = false; // if more items are pushed we will want to know
		}
		is_ready = ready();
		if (state == e_waiting && is_ready) {
			//std::cout << "scheduler wants time " <<items.size() << " total items\n";
            DBG_SCHEDULER << "scheduler signaling driver for time\n";
			safeSend(sync,"sched", 5); // tell clockwork we have something to do
			state = e_waiting_cw;
		}
		if (state == e_waiting_cw && is_ready) {
			safeRecv(sync, buf, 10, true, response_len);
            DBG_SCHEDULER << "scheduler received ok from driver\n";
			//std::cout << "scheduler executing\n";
			state = e_running;
		}
		
		while ( state == e_running && is_ready) {
			ScheduledItem *item = next();
			DBG_SCHEDULER << "Scheduled item " << (*item) << " ready. \n"; //(" << items.size() << " items)\n"<< *this;
			pop();
			next_time.tv_sec = 0;
			if (item->package) {
                DBG_SCHEDULER << " handling package on " << item->package->receiver->getName() << "\n";
				item->package->receiver->handle(item->package->message, item->package->transmitter);
				delete item->package;
	            delete item;
			}
			else if (item->action) {
				item->action->getOwner()->push(item->action);
	            delete item;
			}
            MachineInstance::forceIdleCheck();
			is_ready = ready();
		}
		if (state == e_running) {
			//std::cout << "scheduler done\n";
			safeSend(sync,"done", 4);
			safeRecv(sync, buf, 10, true, response_len); // wait for ack from clockwork
			if (next() == 0) { DBG_SCHEDULER << "no more scheduled items\n"; }
			state = e_waiting;
		}
	}
}

