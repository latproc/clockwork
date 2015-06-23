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

#ifndef cwlang_ConnectionManager_h
#define cwlang_ConnectionManager_h

#include <string>
#include <pthread.h>
#include <boost/thread.hpp>
#include <sstream>
#include <map>
#include <zmq.hpp>
#include <set>
#include "Message.h"
#include "value.h"
#include "symboltable.h"
#include "cJSON.h"
#include "rate.h"

#include "MessageEncoding.h"
#include "SocketMonitor.h"

std::string constructAlphaNumericString(const char *prefix, const char *val, const char *suffix, const char *default_name);

class CommunicationPoll {
public:
	CommunicationPoll *instance() {
		if (!instance_) instance_ = new CommunicationPoll; return instance_;
	}
	
private:
	CommunicationPoll *instance_;
	CommunicationPoll();
	CommunicationPoll(const CommunicationPoll &);
	CommunicationPoll &operator=(const CommunicationPoll &);
};

class SingleConnectionMonitor : public SocketMonitor {
public:
	SingleConnectionMonitor(zmq::socket_t &s, const char *snam);
	virtual void on_event_accepted(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_disconnected(const zmq_event_t &event_, const char* addr_);
	virtual void on_event_connected(const zmq_event_t &event_, const char* addr_);
	void setEndPoint(const char *endpt);
	std::string &endPoint() { return sock_addr; }
	
private:
	std::string sock_addr;
	SingleConnectionMonitor(const SingleConnectionMonitor&);
	SingleConnectionMonitor &operator=(const SingleConnectionMonitor&);
};

class MachineShadow {
	SymbolTable properties;
	std::set<std::string> states;
	std::string state;
public:
	void setProperty(const std::string, Value &val);
	Value &getValue(const char *prop);
	void setState(const std::string);
};

class ConnectionManager {
public:
	ConnectionManager();
	virtual ~ConnectionManager() {}
	virtual bool setupConnections() =0;
	virtual bool checkConnections() =0;
	virtual bool checkConnections(zmq::pollitem_t *items, int num_items, zmq::socket_t &cmd) =0;
	
	void setProperty(std::string machine, std::string prop, Value val);
	void setState(std::string machine, std::string new_state);
	virtual int numSocks() =0;
	void abort();
	bool ready() { return rate_limiter.ready(); }
protected:
	pthread_t owner_thread;
	bool aborted;
	std::map<std::string, MachineShadow *> machines;
	RateLimiter rate_limiter;
};

/*
 Subscription Manager - create and maintain a connection to a remote clockwork driver

 The command socket is used to request a channel and a subscriber is created to 
 listen for activity from the server. The main thread can communicate with the 
 server via a request/reply connection to a thread running the subscription manager.

 Commands arriving from the program's main thread
 are forwarded remote driver through the command socket.

 */
class SubscriptionManager : public ConnectionManager {
public:
	enum RunStatus { e_waiting_cmd, e_waiting_response };
	enum Status{e_not_used, e_startup, e_disconnected, e_waiting_connect,
		e_settingup_subscriber, e_waiting_subscriber, e_waiting_setup, e_done };

	SubscriptionManager(const char *chname, Protocol proto = eCHANNEL,
						const char *remote_host = "localhost", int remote_port = 5555);
	virtual ~SubscriptionManager();
	void setSetupMonitor(SingleConnectionMonitor *monitor);
	
	void init();
	
	bool requestChannel();
	
	bool setupConnections();
	bool checkConnections(); // test whether setup and subscriber channels are connected

	// the following variants poll for activity and handle commands arriving
	// on the command channel or pass them to the remote party through the subscriber or setup channel
	bool checkConnections(zmq::pollitem_t *items, int num_items, zmq::socket_t &cmd);
	bool checkConnections(zmq::pollitem_t *items, int num_items);
	virtual int numSocks() { return 2; }
	int configurePoll(zmq::pollitem_t *);

	zmq::socket_t &subscriber();
	zmq::socket_t &setup(); // invalid for non-client instances
	bool isClient(); // only clients have a setup socket

	Status setupStatus() const { return _setup_status; } // always e_not_used for non client instances
	void setSetupStatus( Status new_status );
	uint64_t state_start;
	RunStatus run_status;
	int subscriber_port;
	std::string current_channel;
	std::string subscriber_host;
	std::string channel_name;
	std::string channel_url;
	Protocol protocol;
	SingleConnectionMonitor monit_subs;
	SingleConnectionMonitor *monit_pubs;
	SingleConnectionMonitor *monit_setup;
protected:
	zmq::socket_t subscriber_;
	// A server instance will not have a socket for setting up the subscriber
	zmq::socket_t *setup_;
	Status _setup_status;
};

#endif
