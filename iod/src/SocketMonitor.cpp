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

#include <assert.h>
#include "MessagingInterface.h"
#include <iostream>
#include <exception>
#include <math.h>
#include <zmq.hpp>
#include <map>
#include <inttypes.h>
#include "Logger.h"
#include "DebugExtra.h"
#include "cJSON.h"
#ifdef DYNAMIC_VALUES
#include "dynamic_value.h"
#endif
#include "symboltable.h"
#include "options.h"
#include "anet.h"
#include "MessageLog.h"
#include "Dispatcher.h"
#include "MessageEncoding.h"
#include "Channel.h"
#include "SocketMonitor.h"

static std::string constructSocketName() {
	static int sequence = 0;
	char buf[40];
	snprintf(buf, 40, "inproc://s_%" PRId64 "_%d", microsecs(), ++sequence);
	return buf;
}

SocketMonitor::SocketMonitor(zmq::socket_t &s) : sock(s), disconnected_(true), aborted(false), active_(false), monitor_socket_name(constructSocketName()) {
}

SocketMonitor::~SocketMonitor() {
}

void SocketMonitor::operator()() {
		char thread_name[100];
		snprintf(thread_name, 100, "iod skt monitor %s", monitor_socket_name.c_str());
#ifdef __APPLE__
        pthread_setname_np(thread_name);
#else
        pthread_setname_np(pthread_self(), thread_name);
#endif
	int exception_count = 0;
    while (!aborted) {
        try {
            monitor(sock, monitor_socket_name.c_str());
			exception_count = 0;
        }
        catch (const zmq::error_t &io) {
            NB_MSG << "ZMQ error " << errno << ": "<< zmq_strerror(errno) << " in socket monitor\n";
			if (errno == 88)
				exit(0);
				//if (errno != EAGAIN && errno != EINTR)
				// monitoring a socket that has been removed. exit and rely on restart code (TBD)
				//	exit(2);
			++exception_count;
			if (exception_count > 5)
				exit(EXIT_FAILURE);
			usleep(100);
        }
        catch (const std::exception &ex) {
            NB_MSG << "unknown exception: " << ex.what() << " monitoring a socket\n";
			++exception_count;
			if (exception_count > 5)
				exit(EXIT_FAILURE);
			usleep(100);
        }
    }
}

void SocketMonitor::abort() {
	zmq::monitor_t::abort();
	aborted = true;
	active_ = false;
}

bool SocketMonitor::active() {
	if (!active_) {
		DBG_MSG << monitor_socket_name << " " << std::hex << this << std::dec <<  " monitor not active\n";
	}
	return active_;
}

const std::string &SocketMonitor::monitorSocketName() const { return monitor_socket_name; }

void SocketMonitor::setMonitorSocketName(std::string name) {
	monitor_socket_name = name;
}

void SocketMonitor::on_monitor_started() {
	DBG_MSG << monitor_socket_name << " " << std::hex << this << std::dec <<  " monitor started\n";
	active_ = true;
}


void SocketMonitor::checkResponders(const zmq_event_t &event_, const char *addr_)const {
	std::multimap<int, EventResponder*>::const_iterator found = responders.find(event_.event);
	while (found != responders.end()) {
		std::pair<int, EventResponder *>curr = *found++;
		if (curr.first != event_.event) continue;
		curr.second->operator()(event_, addr_);
	}
}


void SocketMonitor::on_event_connected(const zmq_event_t &event_, const char* addr_) {
  DBG_MSG << monitor_socket_name<< " on_event_connected " << addr_ << "\n";
  disconnected_ = false;
	checkResponders(event_, addr_);
}
void SocketMonitor::on_event_connect_delayed(const zmq_event_t &event_, const char* addr_) {
	disconnected_ = true;
  //DBG_MSG << socket_name << " on_event_connect_delayed " << addr_ << "\n";
}
void SocketMonitor::on_event_connect_retried(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << socket_name<< " on_event_connect_retried " << addr_ << "\n";
}
void SocketMonitor::on_event_listening(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << socket_name<< " on_event_listening " << addr_ << "\n";
}
void SocketMonitor::on_event_bind_failed(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << socket_name<< " on_event_bind_failed " << addr_ << "\n";
}
void SocketMonitor::on_event_accepted(const zmq_event_t &event_, const char* addr_) {
	//DBG_MSG << socket_name << " on_event_accepted " << event_.value << " " << addr_ << "\n";
	disconnected_ = false;
	checkResponders(event_, addr_);
	}
void SocketMonitor::on_event_accept_failed(const zmq_event_t &event_, const char* addr_) {
	//DBG_MSG << socket_name<< " on_event_accept_failed " << addr_ << "\n";
}
void SocketMonitor::on_event_closed(const zmq_event_t &event_, const char* addr_) {
	disconnected_ = true;
	//DBG_MSG << socket_name<< " on_event_closed " << addr_ << "\n";
}
void SocketMonitor::on_event_close_failed(const zmq_event_t &event_, const char* addr_) {
	//DBG_MSG << socket_name<< " on_event_close_failed " << addr_ << "\n";
}
void SocketMonitor::on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
	//DBG_MSG << socket_name<< " on_event_disconnected "<< event_.value << " "  << addr_ << "\n";
	disconnected_ = true;
	checkResponders(event_, addr_);
}
void SocketMonitor::on_event_unknown(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << monitor_socket_name << " on_event_unknown " << addr_ << "\n";
}
    
bool SocketMonitor::disconnected() { return disconnected_;}

void SocketMonitor::addResponder(uint16_t event, EventResponder *responder) {
    responders.insert(std::pair<int, EventResponder*>(event, responder));
}
void SocketMonitor::removeResponder(uint16_t event, EventResponder *responder) {
    std::multimap<int, EventResponder*>::iterator found = responders.find(event);
    while (found != responders.end()) {
        std::pair<int, EventResponder *>curr = *found;
        if (curr.first != event) break;
        if (curr.second == responder) found = responders.erase(found); else found++;
    }
}

