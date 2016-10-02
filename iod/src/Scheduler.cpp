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
#include "watchdog.h"
#include "ProcessingThread.h"

Scheduler *Scheduler::instance_;
static const uint32_t ONEMILLION = 1000000L;
static Watchdog *wd = 0;

class SchedulerInternals {
public:
	boost::recursive_mutex q_mutex;
};

#if 0
class scheduler_queue_scoped_lock {
public:
	scheduler_queue_scoped_lock( boost::recursive_mutex &mut) : mutex(mut) { mutex.lock(); }
	~scheduler_queue_scoped_lock() { mutex.unlock(); }
	boost::recursive_mutex &mutex;
};
#endif

static uint64_t calcDeliveryTime(long delay) {
	uint64_t res = microsecs() + delay;
	if (delay<0) {
		DBG_SCHEDULER << "***** negative delta: " << delay << "\n";
	}
	DBG_SCHEDULER << "setTime: " << res << "\n";
	return res;
}

std::string Scheduler::getStatus() { 
	boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
	uint64_t now = microsecs();
	long wait_duration = 0;
	if (notification_sent) wait_duration = now - notification_sent;
	char buf[300];
	if (state == e_running) 
		snprintf(buf, 300, "busy");
	else
		snprintf(buf, 300, "state: %d, is ready: %d"
			" items: %ld, now: %ld\n"
			"last notification: %10.6f\nwait time: %10.6f",
				 state, ready(now), items.size(), (now - ProcessingThread::programStartTime()),
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
	return delivery_time < other.delivery_time;
}

bool ScheduledItem::operator>=(const ScheduledItem& other) const {
	return delivery_time >= other.delivery_time;
}

ScheduledItem *Scheduler::next() const { 
	boost::recursive_mutex::scoped_lock scoped_lock(internals->q_mutex);
	if (items.empty()) return 0; else return items.top(); 
}

void Scheduler::pop() { 
	boost::recursive_mutex::scoped_lock scoped_lock(internals->q_mutex);
	//items.check();
	items.pop(); 
}

ScheduledItem* PriorityQueue::top() const { 
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

ScheduledItem::ScheduledItem(long delay, Package *p) :package(p), action(0) {
	delivery_time = calcDeliveryTime(delay);
	DBG_SCHEDULER << "scheduled package: " << delivery_time << "\n";
}

ScheduledItem::ScheduledItem(long delay, Action *a) :package(0), action(a) {
	delivery_time = calcDeliveryTime(delay);
	DBG_SCHEDULER << "scheduled action: " << delivery_time << "\n";
}

ScheduledItem::ScheduledItem(uint64_t starting, long delay, Action *a) : package(0), action(a) {
	delivery_time = starting + delay;
	DBG_SCHEDULER << "scheduled action: " << delivery_time << "\n";
}


ScheduledItem::~ScheduledItem() {
}

std::ostream &ScheduledItem::operator <<(std::ostream &out) const {
	uint64_t now = microsecs();
	int64_t delta = delivery_time - now;
	out << delivery_time << " (" << delta << ") ";
	if (package) out << *package; else if (action) out << *action;
	return out;
}

std::ostream &operator <<(std::ostream &out, const ScheduledItem &item) {
    return item.operator<<(out);
}

Scheduler::Scheduler() : state(e_waiting), update_sync(*MessagingInterface::getContext(), ZMQ_PAIR),
		update_notify(0), next_delay_time(0), notification_sent(0) {
	internals = new SchedulerInternals;
	wd = new Watchdog("Scheduler", 300, false);
	update_sync.bind("inproc://sch_items");
	next_time = 0;
}

bool CompareSheduledItems::operator()(const ScheduledItem *a, const ScheduledItem *b) const {
    bool result = (*a) >= (*b);
    return result;
}

int64_t Scheduler::getNextDelay() {
	if (empty()) return -1;
    ScheduledItem *top = next();
	next_time = top->delivery_time;
	return next_time - nowMicrosecs();
}

int64_t Scheduler::getNextDelay(uint64_t start) {
	if (empty()) return -1;
	ScheduledItem *top = next();
	return top->delivery_time- start;
}

void Scheduler::add(ScheduledItem*item) {
	ScheduledItem *top = 0;
	{
		boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
		DBG_SCHEDULER << "Scheduling item: " << *item << "\n";
		//DBG_SCHEDULER << "Before schedule::add() " << *this << "\n";
		items.push(item);
	}
	top = next();
	next_time = top->delivery_time;
	next_delay_time = getNextDelay();
#if 0
	uint64_t last_notification = notification_sent; // this may be changed by the scheduler
	long wait_duration = nowMicrosecs() - last_notification;
	if (ready() ||  !last_notification) {
		if (!update_notify) {
			update_notify = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PAIR);
			update_notify->connect("inproc://sch_items");
		}
		wd->start();
		while (true) {
			wd->poll();
			try {
				safeSend(*update_notify,"poke",4);
				notification_sent = nowMicrosecs();
				break;
			}		
			catch(zmq::error_t err) {
				if (zmq_errno() == EINTR || zmq_errno() == EAGAIN) continue;
				
				char errmsg[100];
				snprintf(errmsg, 100, "Scheduler::add error: %s", zmq_strerror( zmq_errno()));
				std::cerr << errmsg << "\n";
				MessageLog::instance()->add(errmsg);
				delete update_notify;
				update_notify = 0;
				wd->stop();
				throw;
			} 
		}
		wd->stop();
	}
	else {
		if (wait_duration >= 1000000L && item->action)
		{
			FileLogger fl(program_name);
			fl.f() << "scheduler waiting for a long time for a response: " << wait_duration << "\n";
		}
		//assert (wait_duration < 1000000L);
	}
#endif
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

bool Scheduler::ready(uint64_t start) {
	boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
	if (items.empty()) { return false; }
	return getNextDelay(start) <= 0;
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
	zmq::socket_t sync(*MessagingInterface::getContext(), ZMQ_REP);
	sync.bind("inproc://scheduler_sync");
	char buf[10];
	size_t response_len = 0;
	while (!  safeRecv(sync, buf, 10, true, response_len, 0)) usleep(100);
	NB_MSG << "Scheduler started\n";

	state = e_waiting;
	bool is_ready;
	uint64_t last_poll = nowMicrosecs();
	while (state != e_aborted) {
		next_delay_time = getNextDelay(last_poll);
		if (!ready(last_poll) && state == e_waiting) {
			wd->stop();
			long delay = next_delay_time;
			if (delay>1500) usleep(1000); else usleep(100);
#if 0
			notification_sent = 0; // checking now, if more items are pushed we will want to know
			bool no_items = false;
			{
				boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
				no_items = items.empty();
			}
			if (no_items) {
				// empty, just wait for someone to add some work
				if (!safeRecv(update_sync, buf, 10, false, response_len, 5)) {
					//NB_MSG << "Scheduler: safeRecv failed at line " << __LINE__ << "\n";
				}
			}
			else if (delay <= 0) {
				if (!safeRecv(update_sync, buf, 10, false, response_len, 0)) {
					//NB_MSG << "Scheduler: safeRecv failed at line " << __LINE__ << "\n";
				}
			}
			else if (delay < 1000) {
				if (!safeRecv(update_sync, buf, 10, false, response_len, 0)) {
					//std::cout << "Scheduler: safeRecv failed at line " << __LINE__ << "\n";
				}
				usleep((unsigned int)delay);
			}
			else
				if (!safeRecv(update_sync, buf, 10, false, response_len, delay/1000)) {
					//std::cout << "Scheduler: safeRecv failed at line " << __LINE__ << "\n";
				}
#endif
		}
		last_poll = nowMicrosecs();
		is_ready = ready(last_poll);
		if (!is_ready && state == e_waiting) continue;

		if ( state == e_waiting && is_ready) {
            DBG_SCHEDULER << "scheduler signaling driver for time\n";
			safeSend(sync,"sched", 5); // tell clockwork we have something to do
			state = e_waiting_cw;
		}
		if (state == e_waiting_cw) {
			safeRecv(sync, buf, 10, true, response_len, 0);
            DBG_SCHEDULER << "scheduler received ok from driver\n";
			state = e_running;
		}
		
		int items_found = 0;
		//is_ready = ready();
		while ( state == e_running && is_ready) {
			wd->poll();
			ScheduledItem *item = 0;
			{
				boost::recursive_mutex::scoped_lock scoped_lock(Scheduler::instance()->internals->q_mutex);
				item = next();
				DBG_SCHEDULER << "Scheduler activating scheduled item " << (*item) << " ready. " << items.size() << " items remain\n";
				pop();
			}
			next_time = 0;
			if (item->package) {
				DBG_SCHEDULER << "Scheduler activating package on " << item->package->receiver->getName() << "\n";
				item->package->receiver->handle(item->package->message, item->package->transmitter);
				delete item->package;
				delete item;
				++items_found;
			}
			else if (item->action) {
				DBG_SCHEDULER << "Scheduler activating pushing action to  " << item->action->getOwner()->getName() << "\n";
				item->action->getOwner()->push(item->action);
				delete item;
				++items_found;
			}
			is_ready = ready(last_poll);
		}
		//if (items_found)
		//	MachineInstance::forceIdleCheck();
		if (state == e_running) {
			DBG_SCHEDULER << "scheduler done\n";
			safeSend(sync,"done", 4);
			if (next() == 0) { DBG_SCHEDULER << "no more scheduled items, waiting for cw ack\n"; }
			while (!safeRecv(sync, buf, 10, true, response_len, 100)); // wait for ack from clockwork
			if (next() == 0) { DBG_SCHEDULER << "no more scheduled items\n"; }
			state = e_waiting;
			wd->stop();
		}
	}
}

