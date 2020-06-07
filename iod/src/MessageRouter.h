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

#ifndef cwlang_MessageRouter_h
#define cwlang_MessageRouter_h

#include <string>
#include <sstream>
#include <map>
#include <zmq.hpp>

class MessageRouterInternals;
class MessageRouter {
public:
	MessageRouter();
	~MessageRouter();

	void operator()();
	void finish();
	void poll();
	void addRoute(int route_id, int type, const std::string address);
	void addDefaultRoute(int type, const std::string address);
	void addRemoteSocket(int type, const std::string address);

	// used when not running as a thread
	void addRoute(int route_id, zmq::socket_t *dest);
	void addDefaultRoute(zmq::socket_t *def);
	void setRemoteSocket(zmq::socket_t *remote_sock);

	void removeRoute(int route_id);

	void addFilter(int route_id, MessageFilter *filter);
	void removeFilter(int route_id, MessageFilter *filter);

private:
	MessageRouter(const MessageRouter &other);
	MessageRouter &operator=(const MessageRouter &other);
	MessageRouterInternals *internals;
};

#endif
