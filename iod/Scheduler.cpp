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
#include <boost/thread/mutex.hpp>
#include <assert.h>

Scheduler *Scheduler::instance_;
static const uint32_t ONEMILLION = 1000000L;

static void setTime(struct timeval &now, long delta) {
	gettimeofday(&now, 0);
	if (delta<0) { 
		DBG_SCHEDULER << "***** negative delta: " << delta << "\n"; 
	}
	else {
		now.tv_sec += delta / ONEMILLION;
		now.tv_usec += delta % ONEMILLION;
		if (now.tv_usec >= ONEMILLION) {
			++now.tv_sec;
			now.tv_usec -= ONEMILLION;
		}
	}
	DBG_SCHEDULER << "setTime: " << now.tv_sec << '.' << std::setfill('0')<< std::setw(6) << now.tv_usec << "\n";
}

std::string Scheduler::getStatus() { 

	boost::mutex::scoped_lock(q_mutex);
	struct timeval now;
	gettimeofday(&now, 0);
	long wait_duration = 0;
	if (notification_sent) wait_duration = get_diff_in_microsecs(&now, notification_sent);
	char buf[300];
	if (state == e_running) 
		snprintf(buf, 300, "busy");
	else
		snprintf(buf, 300, "state: %d, is ready: %d"
			" items: %ld, now: %ld.%06ld\n"
			"last notification: %10.6f\nwait time: %10.6f",
			state, ready(), items.size(), now.tv_sec, now.tv_usec, 
				(float)notification_sent/1000000.0f, (float)wait_duration/1000000.0f);
	std::stringstream ss;
	ss << buf << "\n";
    std::list<ScheduledItem*>::const_iterator iter = items.queue.begin();
	while (iter != items.queue.end()) {
        ScheduledItem *item = *iter++;
		ss << *item << "\n";
	}
	ss << std::ends;
	return ss.str();
}

bool ScheduledItem::operator<(const ScheduledItem& other) const {
    if (delivery_time.tv_sec < other.delivery_time.tv_sec) return true;
    if (delivery_time.tv_sec == other.delivery_time.tv_sec) 
		return delivery_time.tv_usec < other.delivery_time.tv_usec;
    return false;
}

bool ScheduledItem::operator>=(const ScheduledItem& other) const {
    if (delivery_time.tv_sec > other.delivery_time.tv_sec) return true;
    if (delivery_time.tv_sec == other.delivery_time.tv_sec) 
		return delivery_time.tv_usec >= other.delivery_time.tv_usec;
    return false;
}

ScheduledItem *Scheduler::next() const { 
	boost::mutex::scoped_lock(q_mutex);
	if (items.empty()) return 0; else return items.top(); 
}

void Scheduler::pop() { 
	boost::mutex::scoped_lock(q_mutex);
	//items.check();
	items.pop(); 
}

ScheduledItem* PriorityQueue::top() const { 
	boost::mutex::scoped_lock(q_mutex);
	return queue.front(); 
}
bool PriorityQueue::empty() const { 
	return queue.empty();
}
void PriorityQueue::pop() { 
	boost::mutex::scoped_lock(q_mutex);
	queue.pop_front(); 
}
size_t PriorityQueue::size() const { 
	return queue.size(); 
}

void PriorityQueue::push(ScheduledItem *item) {
	boost::mutex::scoped_lock(q_mutex);
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

bool PriorityQueue::check() const {
	ScheduledItem *prev = 0;
	std::list<ScheduledItem*>::const_iterator iter = queue.begin();
	while (iter!= queue.end()) {
		if (prev == 0) {
			prev = *iter++;
		}
		else {
			ScheduledItem *cur = *iter++;
			assert( *cur >= *prev );
		}
	}
	return true;
}

ScheduledItem::ScheduledItem(long when, Package *p) :package(p), action(0) {
	setTime(delivery_time, when);
	DBG_SCHEDULER << "scheduled package: " << delivery_time.tv_sec << std::setfill('0')<< std::setw(6) << delivery_time.tv_usec << "\n";
}

ScheduledItem::ScheduledItem(long when, Action *a) :package(0), action(a) {
	setTime(delivery_time, when);
	DBG_SCHEDULER << "scheduled action: " << delivery_time.tv_sec << "." << std::setfill('0')<< std::setw(6) << delivery_time.tv_usec << "\n";
}

std::ostream &ScheduledItem::operator <<(std::ostream &out) const {
	struct timeval now;
	gettimeofday(&now, 0);
    int64_t delta = get_diff_in_microsecs( &delivery_time, &now);
    out << delivery_time.tv_sec << "." << std::setfill('0')<< std::setw(6) << delivery_time.tv_usec
		<< " (" << delta << ") ";
    if (package) out << *package; else if (action) out << *action;
    return out;
}

std::ostream &operator <<(std::ostream &out, const ScheduledItem &item) {
    return item.operator<<(out);
}

Scheduler::Scheduler() : state(e_waiting), update_sync(*MessagingInterface::getContext(), ZMQ_PULL), next_delay_time(0), notification_sent(0) { 
	update_sync.bind("inproc://sch_items");
	next_time.tv_sec = 0;
	next_time.tv_usec = 0;
}

bool CompareSheduledItems::operator()(const ScheduledItem *a, const ScheduledItem *b) const {
    bool result = (*a) >= (*b);
    return result;
}

int64_t Scheduler::getNextDelay() {
	if (empty()) return -1;
    ScheduledItem *top = next();
	next_time = top->delivery_time;
    return get_diff_in_microsecs(&next_time, nowMicrosecs());
}

void Scheduler::add(ScheduledItem*item) {
    ScheduledItem *top = 0;
	boost::mutex::scoped_lock(q_mutex);
	DBG_SCHEDULER << "Scheduling item: " << *item << "\n";
	//DBG_SCHEDULER << "Before schedule::add() " << *this << "\n";
	items.push(item);
    top = next();
	next_time = top->delivery_time;
    next_delay_time = getNextDelay();
	//assert(next_delay_time < 60000000L);
	uint64_t last_notification = notification_sent; // this may be changed by the scheduler
	if (!last_notification) {
		zmq::socket_t update_notify(*MessagingInterface::getContext(), ZMQ_PUSH);
		int send_state = 0; // disconnected
		while (true) {
			try {
				if (send_state == 0) {
					update_notify.connect("inproc://sch_items");
					send_state = 1; // connected
				}
				safeSend(update_notify,"poke",4);
				notification_sent = nowMicrosecs();
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
	else {
		long wait_duration = nowMicrosecs() - last_notification;
		if (wait_duration >= 60L * 1000000L && item->action) std::cout << "long delay of " << wait_duration << " for " << item->action->getOwner()->getName() << "\n";
		assert (wait_duration < 60L * 1000000L);
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
	if (items.empty()) { return false; }
	boost::mutex::scoped_lock(q_mutex);
	return getNextDelay() <= 0;
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
		next_delay_time = getNextDelay();
		if (!ready() && state == e_waiting) {
#if 0
			if (items.empty()) {
				DBG_SCHEDULER << "scheduler waiting for work " 
					<< next_delay_time << " N: " << items.size() << "\n";
			}
#endif
			long delay = next_delay_time;
			if (items.empty()) {
				// empty, just wait for someone to add some work
				safeRecv(update_sync, buf, 10, false, response_len, 100);
			}
			else if (delay <= 0) {
				safeRecv(update_sync, buf, 10, false, response_len, 0);
			}
			else if (delay < 1000) {
				safeRecv(update_sync, buf, 10, false, response_len, 0);
				usleep((unsigned int)delay);
			}
			else
				safeRecv(update_sync, buf, 10, false, response_len, delay/1000);
			notification_sent = 0; // if more items are pushed we will want to know
		}
		is_ready = ready();
		if (state == e_waiting && is_ready) {
            DBG_SCHEDULER << "scheduler signaling driver for time\n";
			safeSend(sync,"sched", 5); // tell clockwork we have something to do
			state = e_waiting_cw;
		}
		if (state == e_waiting_cw) {
			safeRecv(sync, buf, 10, true, response_len);
            DBG_SCHEDULER << "scheduler received ok from driver\n";
			state = e_running;
		}
		
		int items_found = 0;
		is_ready = ready();
		while ( state == e_running && is_ready) {
			ScheduledItem *item = 0;
			{
				boost::mutex::scoped_lock(q_mutex);
				item = next();
				DBG_SCHEDULER << "Scheduled item " << (*item) << " ready. " << items.size() << " items remain\n";
				pop();
			}
			next_time.tv_sec = 0;
			next_time.tv_usec = 0;
			if (item->package) {
                DBG_SCHEDULER << " handling package on " << item->package->receiver->getName() << "\n";
				item->package->receiver->handle(item->package->message, item->package->transmitter);
				delete item->package;
	            delete item;
				++items_found;
			}
			else if (item->action) {
                DBG_SCHEDULER << " pushing action to  " << item->action->getOwner()->getName() << "\n";
				item->action->getOwner()->push(item->action);
	            delete item;
				++items_found;
			}
			is_ready = ready();
		}
		if (items_found)
            MachineInstance::forceIdleCheck();
		if (state == e_running) {
			//std::cout << "scheduler done\n";
			safeSend(sync,"done", 4);
			safeRecv(sync, buf, 10, true, response_len); // wait for ack from clockwork
			if (next() == 0) { DBG_SCHEDULER << "no more scheduled items\n"; }
			state = e_waiting;
		}
	}
}

