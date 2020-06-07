/*
	Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

	This file is part of Latproc

	Latproc is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
	
	Latproc is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Latproc; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA	02110-1301, USA.
*/

#include <assert.h>
#include "MessagingInterface.h"
#include <iostream>
#include <exception>
#include <math.h>
#include <zmq.hpp>
#include <map>
#include "Logger.h"
#include "DebugExtra.h"
#include "MessageLog.h"
#include "SocketMonitor.h"
#include "ConnectionManager.h"

class RouteInfo {
public:
	enum Facing {both, request, reply} facing;
	int kind; // type of socket
	std::string address;
	zmq::socket_t *sock;
	std::list<MessageFilter*> filters;

	RouteInfo(const char *addr, int kind = ZMQ_PAIR, RouteInfo::Facing dir = both);
	RouteInfo(zmq::socket_t *s);
	RouteInfo(const char *addr, zmq::socket_t *s);
};

RouteInfo::RouteInfo(const char *addr, int type, Facing dir)
: facing(dir), kind(type), address(addr), sock(0)
{
	if (kind == ZMQ_REQ) facing = request;
	else if (kind == ZMQ_REP) facing = reply;
}

RouteInfo::RouteInfo(zmq::socket_t *s)
: facing(both), kind(ZMQ_PAIR), address("undefined"), sock(s) {
}

RouteInfo::RouteInfo(const char *addr, zmq::socket_t *s)
:facing(both), kind(ZMQ_PAIR), address(addr), sock(s)
{
}


class MessageRouterInternals {
public:
	MessageRouterInternals() 
		: remote(0),default_dest(0),done(false),
			destinations(0), saved_num_items(0),items(0)
 {}
	boost::mutex data_mutex;
	std::map<int, RouteInfo *> routes;
	std::list<MessageFilter*> filters;

	zmq::socket_t *remote;
	zmq::socket_t *default_dest;
	bool done;
	int *destinations;
	size_t saved_num_items;
	zmq::pollitem_t *items;
};

MessageRouter::MessageRouter()
{
	internals = new MessageRouterInternals;
}

void MessageRouter::finish() {
	internals->done = true;
}
class scoped_lock {
public:
	scoped_lock( const char *loc, boost::mutex &mut) : location(loc), mutex(mut), lock(mutex, boost::defer_lock) {
		//		DBG_CHANNELS << location << " LOCKING...\n";
		lock.lock();
		//		DBG_CHANNELS << location << " LOCKED...\n";
	}
	~scoped_lock() {
		lock.unlock();
		//		DBG_CHANNELS << location << " UNLOCKED\n";
	}
	std::string location;
	boost::mutex &mutex;
	boost::unique_lock<boost::mutex> lock;
};


void addRoute(int route_id, zmq::socket_t *dest);
void addDefaultRoute(zmq::socket_t *def);
void setRemoteSocket(zmq::socket_t *remote_sock);

void MessageRouter::addRoute(int route_id, zmq::socket_t *dest) {
	scoped_lock lock("addRoute", internals->data_mutex);

	if (internals->routes.find(route_id) == internals->routes.end()) {
		DBG_CHANNELS << " added route " << route_id << "\n";
		internals->routes[route_id] = new RouteInfo(dest);
	}
}

void MessageRouter::addDefaultRoute(zmq::socket_t *def) {
	internals->default_dest = def;
}

void MessageRouter::setRemoteSocket(zmq::socket_t *remote_sock) {
	internals->remote = remote_sock;
}

void MessageRouter::addRoute(int route_id, int type, const std::string address) {
	scoped_lock lock("addRoute", internals->data_mutex);

	if (internals->routes.find(route_id) == internals->routes.end()) {
		internals->routes[route_id] = new RouteInfo(address.c_str());
		zmq::socket_t *dest = new zmq::socket_t(*MessagingInterface::getContext(), type);
		dest->connect(address.c_str());
		internals->routes[route_id]->sock = dest;
	}
}

void MessageRouter::addDefaultRoute(int type, const std::string address) {
	scoped_lock lock("addDefaultRoute", internals->data_mutex);
	zmq::socket_t *def = new zmq::socket_t(*MessagingInterface::getContext(), type);
	def->connect(address.c_str());
	internals->default_dest = def;
}
void MessageRouter::addRemoteSocket(int type, const std::string address) {
	scoped_lock lock("setRemoteSocket", internals->data_mutex);
	zmq::socket_t *remote_sock = new zmq::socket_t(*MessagingInterface::getContext(), type);
	DBG_CHANNELS << "MessageRouter connecting to remote socket " << address << "\n";
	remote_sock->connect(address.c_str());
	internals->remote = remote_sock;
}
void MessageRouter::operator()() {
	boost::unique_lock<boost::mutex> lock(internals->data_mutex);
	int *destinations = 0;
	size_t saved_num_items = 0;
	zmq::pollitem_t *items;
	while (!internals->done) {
		poll();
		usleep(20);
	}
}

void MessageRouter::addFilter(int route_id, MessageFilter *filter) {
	if (route_id == MessageHeader::SOCK_REMOTE) {
		internals->filters.push_back(filter);
	}
	else if (internals->routes.find(route_id) != internals->routes.end()) {
		RouteInfo *ri = internals->routes[route_id];
		ri->filters.push_back(filter);
	}
}

void MessageRouter::removeFilter(int route_id, MessageFilter *filter) {
	if (route_id == MessageHeader::SOCK_REMOTE) {
		internals->filters.remove(filter);
	}
	else if (internals->routes.find(route_id) != internals->routes.end()) {
		RouteInfo *ri = internals->routes[route_id];
		ri->filters.remove(filter);
	}
}

void MessageRouter::poll() {
	boost::unique_lock<boost::mutex> lock(internals->data_mutex);
	if (!internals->remote) {
		usleep(10);
		return;
	}
	unsigned int num_socks = internals->routes.size()+1;

	if (internals->saved_num_items != num_socks) {
		if (internals->destinations) delete[] internals->destinations;
		internals->destinations = new int[num_socks];
		if (internals->items) delete internals->items;
		internals->items = new zmq::pollitem_t[num_socks];
	}
	zmq::pollitem_t *items = internals->items;
	int *destinations = internals->destinations;


	items[0].socket = (void*)(*internals->remote);
	items[0].fd = 0;
	items[0].events = ZMQ_POLLIN;
	items[0].revents = 0;
	int idx = 1;

	std::map<int, RouteInfo *>::iterator iter = internals->routes.begin();
	while ( iter != internals->routes.end()) {
		std::pair<int, RouteInfo*>item = *iter++;
		items[idx].socket = (void*)(*(item.second)->sock);
		items[idx].fd = 0;
		items[idx].events = ZMQ_POLLIN | ZMQ_POLLERR;
		items[idx].revents = 0;
		destinations[idx-1] = item.first;
		++idx;
	}

	try {
		int rc = zmq::poll(items, num_socks, 0);
		if (rc == 0) {
			return;
		}
	}
	catch (zmq::error_t zex) {
		{
			FileLogger fl(program_name);
			fl.f() << "MessageRouter zmq error " << zmq_strerror(zmq_errno()) << "\n";
		}
		if (zmq_errno() == EINTR) { return; }
		assert(false);
	}
	for (unsigned int i=0; i<num_socks; ++i) if (items[i].revents & ZMQ_POLLERR) {
		DBG_CHANNELS << "MessageRouter poll(): zmq error on sock " << i << "\n";
	}
	int c = 0;
	for (unsigned int i=0; i<num_socks; ++i) if (items[i].revents & ZMQ_POLLIN)++c;
	if (!c) return;

#if 1
	if (c) {
		std::cout << "activity: ";
		for (unsigned int i=0; i<num_socks; ++i) {
			if (items[i].revents & ZMQ_POLLIN) std::cout << i << " " << (i==0) << " ";
		}
		std::cout << "\n";
	}
#endif

	char *buf = 0;
	size_t len = 0;
	int source = 0;
	MessageHeader mh;

	// receiving from remote socket
	if (items[0].revents & ZMQ_POLLIN) {
		DBG_CHANNELS << "Message router collecting message from remote\n";
		if (safeRecv(*internals->remote, &buf, &len, false, 0, mh)) {
			DBG_CHANNELS << "Message router collected message from " << mh.source << " for route " << mh.dest << "\n";
			std::map<int, RouteInfo *>::const_iterator found = internals->routes.find(mh.dest);
			if (found != internals->routes.end()) {
				RouteInfo * ri = internals->routes.at(mh.dest);
				zmq::socket_t *dest = ri->sock;
				MessageFilter *filter = 0;
				std::list<MessageFilter *>::iterator found_filter = ri->filters.begin();
				if ( found_filter != ri->filters.end() ) {
					{
						FileLogger fl(program_name);
						fl.f() << "Filtering " <<buf << "\n";
					}
					filter = *found_filter;
					if ( !filter->filter(&buf, len, mh ) ) {
						DBG_CHANNELS << "filter rejected message " << buf << "\n";
						return;
					}
				}
				DBG_CHANNELS << "forwarding " << buf << " to " << mh.dest << "\n";
				mh.start_time = microsecs();
				safeSend(*dest, buf, len, mh);
			}
			else if (internals->default_dest) {
				mh.start_time = microsecs();
				safeSend(*internals->default_dest, buf, len);
			}
			else {
				DBG_CHANNELS << "Message " << buf << " needed a default route but none has been set\n";
			}
		}
	}

	// forwarding to remote socket
	for (unsigned int i=1; i<num_socks; ++i) {
		if (items[i].revents & ZMQ_POLLIN) {
			DBG_CHANNELS << "activity on pollidx " << i << " route " << destinations[i-1] <<	"\n";
			std::map<int, RouteInfo *>::iterator found = internals->routes.find(destinations[i-1]);
			assert(found != internals->routes.end());
			zmq::socket_t *sock = internals->routes[ destinations[i-1] ]->sock;
			MessageHeader mh;
			if (safeRecv(*sock, &buf, &len, false, 0, mh)) {
				DBG_CHANNELS << " collected '" << buf << "' from route " << destinations[i-1] << " with header " << mh << "\n";
				bool do_send = true; // by default all messages are sent. a filter can stop that
				std::list<MessageFilter *>::iterator fi = internals->filters.begin();
				while (fi != internals->filters.end()) {
					MessageFilter *filter = *fi++;
					if ( !(filter->filter(&buf, len, mh))) do_send = false;
				}
				if (do_send) {
					char disp[2*len+1];
					for (int i=0; i<len; ++i) sprintf(disp+2*i, "%x", buf[i]);
					disp[2*len] = 0;
					DBG_CHANNELS << "Sending '" << disp << "'\n";
					mh.start_time = microsecs();
					safeSend(*internals->remote, buf, len, mh);
				}
				else
					DBG_CHANNELS << "dropped message due to filter\n";
			}
		}
	}
	delete[] buf;
	usleep(10);
}
