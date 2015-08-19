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

#ifndef cwlang_MessagingInterface_h
#define cwlang_MessagingInterface_h

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

uint64_t nowMicrosecs();
uint64_t nowMicrosecs(const struct timeval &now);
int64_t get_diff_in_microsecs(const struct timeval *now, const struct timeval *then);
int64_t get_diff_in_microsecs(uint64_t now, const struct timeval *then);
int64_t get_diff_in_microsecs(const struct timeval *now, uint64_t then);

enum ProtocolType { eCLOCKWORK, eRAW, eZMQ, eCHANNEL };

bool safeRecv(zmq::socket_t &sock, char *buf, int buflen, bool block, size_t &response_len, uint64_t timeout);
void safeSend(zmq::socket_t &sock, const char *buf, int buflen);

bool sendMessage(const char *msg, zmq::socket_t &sock, std::string &response, uint32_t timeout_us = 0);


class MessagingInterface : public Receiver {
public:
	static const bool DEFERRED_START = true;
	static const bool IMMEDIATE_START = false;

	MessagingInterface(int num_threads, int port, bool deferred_start, ProtocolType proto = eZMQ);
	MessagingInterface(std::string host, int port, bool deferred_start, ProtocolType proto = eZMQ);
	~MessagingInterface();
	void start();
	void stop();
	bool started();
	static zmq::context_t *getContext();
	static void setContext(zmq::context_t *);
	static int uniquePort(unsigned int range_start = 7600, unsigned int range_end = 7799);
	char *send(const char *msg);
	char *send(const Message &msg);
	bool send_raw(const char *msg);
	void setCurrent(MessagingInterface *mi) { current = mi; }
	static MessagingInterface *getCurrent();
	static MessagingInterface *create(std::string host, int port, ProtocolType proto = eZMQ);
	char *sendCommand(std::string cmd, std::list<Value> *params);
	char *sendCommand(std::string cmd, Value p1 = SymbolTable::Null,
					  Value p2 = SymbolTable::Null,
					  Value p3 = SymbolTable::Null);
	char *sendState(std::string cmd, std::string name, std::string state_name);
	
	//Receiver interface
	virtual bool receives(const Message&, Transmitter *t);
	virtual void handle(const Message&, Transmitter *from, bool needs_receipt = false );
	zmq::socket_t *getSocket() { return socket; }
	static bool aborted() { return abort_all; }
	static void abort() { abort_all = true; }
	const std::string &getURL() const { return url; }
	
private:
	static zmq::context_t *zmq_context;
	void connect();
	static MessagingInterface *current;
	ProtocolType protocol;
	zmq::socket_t *socket;
	static std::map<std::string, MessagingInterface *>interfaces;
	bool is_publisher;
	std::string url;
	int connection;
	std::string hostname;
	int port;
	pthread_t owner_thread;
	static bool abort_all;
	bool started_;
};

#endif
