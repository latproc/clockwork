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

#ifndef cwlang_SocketMonitor_h
#define cwlang_SocketMonitor_h

#include <string>
#include <boost/thread.hpp>
#include <sstream>
#include <map>
#include <zmq.hpp>
#include <set>
#include "Message.h"
#include "value.h"
#include "symboltable.h"
#include "cJSON.h"

#include "MessageEncoding.h"

class EventResponder {
public:
	virtual ~EventResponder(){}
	virtual void operator()(const zmq_event_t &event_, const char* addr_) = 0;
};

class SocketMonitor : public zmq::monitor_t {
public:
	SocketMonitor(zmq::socket_t &s, const char *snam);
	void operator()();
	virtual void on_monitor_started();
	virtual void on_event_connected(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_connect_delayed(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_connect_retried(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_listening(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_bind_failed(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_accepted(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_accept_failed(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_closed(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_close_failed(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_disconnected(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_unknown(const zmq_event_t &event_, const char* addr_);
	bool disconnected();
	void abort();
	void addResponder(uint16_t event, EventResponder *responder);
	void removeResponder(uint16_t event, EventResponder *responder);
	void checkResponders(const zmq_event_t &event_, const char *addr_)const;
	void setMonitorSocketName(std::string name);
	const std::string &socketName() const;
	const std::string &monitorSocketName() const;
	
protected:
	std::multimap<int, EventResponder*> responders;
	zmq::socket_t &sock;
	bool disconnected_;
	std::string socket_name;
	bool aborted;
	std::string monitor_socket_name;
};


#endif
