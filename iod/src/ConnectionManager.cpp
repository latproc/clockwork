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
#include "ConnectionManager.h"

static std::string STATE_ERROR("Operation cannot be accomplished in current state");

SingleConnectionMonitor::SingleConnectionMonitor(zmq::socket_t &sm)
    : SocketMonitor(sm) { }

void SingleConnectionMonitor::on_event_accepted(const zmq_event_t &event_, const char* addr_) {
	SocketMonitor::on_event_accepted(event_, addr_);
	DBG_CHANNELS << socket_name << "Accepted\n";
}

void SingleConnectionMonitor::on_event_connected(const zmq_event_t &event_, const char* addr_) {
	SocketMonitor::on_event_connected(event_, addr_);
	DBG_CHANNELS << socket_name << " Connected\n";
}

void SingleConnectionMonitor::on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
	DBG_CHANNELS << socket_name << " Disconnected\n";
	//sock.disconnect(sock_addr.c_str());
	SocketMonitor::on_event_disconnected(event_, addr_);
}

void SingleConnectionMonitor::setEndPoint(const char *endpt) { sock_addr = endpt; }


void MachineShadow::setProperty(const std::string prop, Value &val) {
    properties.add(prop, val, SymbolTable::ST_REPLACE);
}

const Value &MachineShadow::getValue(const char *prop) {
    return properties.lookup(prop);
}

void MachineShadow::setState(const std::string new_state) {
    state = new_state;
}

class ConnectionManagerInternals {
public:
	virtual ~ConnectionManagerInternals() {}
};

ConnectionManager::ConnectionManager() : internals(0),owner_thread(pthread_self()), aborted(false) {
}

void ConnectionManager::abort() { aborted = true; }

std::string constructAlphaNumericString(const char *prefix, const char *val, const char *suffix, const char *default_name) {
	if (!val)
		return default_name;
	int len = strlen(val);
	if (prefix) len += strlen(prefix);
	if (suffix) len += strlen(suffix);
	char buf[len+1];
	char *q = buf;
	if (prefix) { strcpy(buf, prefix); q+= strlen(prefix); }
	const char *p = val;
	while (*p && len--) {
		if (isalnum(*p)) *q++ = *p;
    ++p;
	}
	*q = 0;
	if (q == buf) // no alpha/num found in the input string
		return default_name;
	if (suffix) strcpy(q, suffix);
	return buf;
}

std::string copyAlphaNumChars(const char *val, const char *default_name) {
	return constructAlphaNumericString(0, val, 0, default_name);
}

class SubscriptionManagerInternals : public ConnectionManagerInternals {

public:
	SubscriptionManagerInternals() : sent_request(false), send_time(0) {

	}

	~SubscriptionManagerInternals() {

	}
	// helpers for connection resume
	bool sent_request;
	uint64_t send_time;
	static const uint64_t channel_request_timeout = 3000000;
};

SubscriptionManager::SubscriptionManager(const char *chname, ProtocolType proto,
										 const char *remote_host, int remote_port, int setup_port_num) :
		subscriber_host(remote_host),
		channel_name(chname), protocol(proto), setup_port(setup_port_num), authority(0),
		subscriber_(*MessagingInterface::getContext(), (protocol == eCLOCKWORK)?ZMQ_SUB:ZMQ_PAIR),
		sender_(0),
		subscriber_port(remote_port),
		monit_subs(subscriber_),
		monit_pubs(0), monit_setup(0),
		setup_(0),
		_setup_status(e_startup), sub_status_(ss_init)
{
	internals = new SubscriptionManagerInternals();
	state_start = microsecs();
	//createSubscriberSocket(chname);
	if (isClient()) {
		setup_ = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
		monit_setup = new SingleConnectionMonitor(*setup_);
		setSetupStatus(e_startup);
		init();

		if (subscriber_host == "*") {
			_setup_status = e_not_used; // This is not a client
			assert(protocol == eCHANNEL);
			char url[100];
			snprintf(url, 100, "tcp://*:%d", subscriber_port);
			subscriber_.bind(url);
		}
	}
	else
		init();
}

SubscriptionManager::~SubscriptionManager() {
}

SubscriptionManager::SubStatus SubscriptionManager::subscriberStatus() { return sub_status_;}

void SubscriptionManager::setSetupMonitor(SingleConnectionMonitor *monitor) {
	assert(isClient());
	if (monit_setup) delete monit_setup;
	monit_setup = monitor;
}

void SubscriptionManager::init() {
	if (protocol == eCLOCKWORK) { // start the subscriber if necessary
		subscriber_.setsockopt (ZMQ_SUBSCRIBE, "", 0);
	}
    boost::thread subscriber_monitor(boost::ref(monit_subs));
	if (isClient()) boost::thread setup_monitor(boost::ref(*monit_setup));
	run_status = e_waiting_cmd;
}

int SubscriptionManager::configurePoll(zmq::pollitem_t *items) {
	int idx = 0;
	if (setup_) {
		// this cast should not be necessary as there is an operator for it
		items[idx].socket = (void*)setup(); 
		items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;
		items[idx].fd = 0;
		items[idx].revents = 0;
		++idx;
	}
	// this cast should not be necessary as there is an operator for it
	items[idx].socket = (void*)subscriber();
	items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;
	items[idx].fd = 0;
	items[idx].revents = 0;
    return ++idx;
}

bool SubscriptionManager::requestChannel() {
	SubscriptionManagerInternals *smi = dynamic_cast<SubscriptionManagerInternals*>(internals);
    size_t len = 0;
	uint64_t now = microsecs();
	assert(isClient());
	if (setupStatus() == SubscriptionManager::e_waiting_connect && !monit_setup->disconnected()
	   ) {
		int error_count = 0;
		if (!smi->sent_request
			|| smi->send_time - now > SubscriptionManagerInternals::channel_request_timeout) {

			DBG_CHANNELS << "Requesting channel " << channel_name << "\n";
			char *channel_setup = MessageEncoding::encodeCommand("CHANNEL", channel_name);
			try {
				usleep(200);
				setSetupStatus(SubscriptionManager::e_waiting_setup);
				safeSend(setup(), channel_setup, strlen(channel_setup));
				smi->sent_request = true;
				smi->send_time = now;
			}
			catch (zmq::error_t ex) {
				++error_count;
				{FileLogger fl(program_name); fl.f() << channel_name<< " exception " << zmq_errno()  << " "
					<< zmq_strerror(zmq_errno()) << " requesting channel\n"<<std::flush; }
				if (zmq_errno() == ETIMEDOUT) {
					// TBD. need propery recovery from this...
					exit(zmq_errno());
					smi->sent_request = false;
				}
				if (zmq_errno() == EFSM /*EFSM*/ || STATE_ERROR == zmq_strerror(zmq_errno())) {
					{
						FileLogger fl(program_name);
						fl.f() << channel_name << " attempting recovery requesting channels\n" << std::flush; }
					zmq::message_t m; setup().recv(&m, ZMQ_DONTWAIT); 
					return false;
				}
			}
		}
		if (error_count >=4) {
			{FileLogger fl(program_name); fl.f() << channel_name << " aborting\n" << std::flush; }
			exit(2);
		}
		return false;
	}
	if (setupStatus() == SubscriptionManager::e_waiting_setup && !monit_setup->disconnected()){
        char buf[1000];
        if (!safeRecv(setup(), buf, 1000, false, len, 2)) {
			return false; // attempt a connection but do not wait very long before giving up
		}
		else {
        	if (len < 1000) buf[len] =0;
					{FileLogger fl(program_name);
					fl.f() << "safeRecv got data: " << buf << " in waiting setup\n";
					}
				}
        if (len == 0) return false; // no data yet
        if (len < 1000) buf[len] =0;
				DBG_CHANNELS << "Got channel " << buf << "\n";
        setSetupStatus(SubscriptionManager::e_settingup_subscriber);
        if (len && len<1000) {
            buf[len] = 0;
            cJSON *chan = cJSON_Parse(buf);
            if (chan) {
                cJSON *port_item = cJSON_GetObjectItem(chan, "port");
                if (port_item) {
                    if (port_item->type == cJSON_Number) {
                        if (port_item->valueNumber.kind == cJSON_Number_int_t)
                            subscriber_port = (int)port_item->valueint;
                        else
                            subscriber_port = ((int)port_item->valuedouble);
                    }
                }
                cJSON *chan_name = cJSON_GetObjectItem(chan, "name");
                if (chan_name && chan_name->type == cJSON_String) {
                    current_channel = chan_name->valuestring;
                }
				cJSON *chan_key = cJSON_GetObjectItem(chan, "authority");
				if (chan_key && chan_key->type == cJSON_Number) {
					authority = chan_key->valueint;
					{FileLogger fl(program_name);
						fl.f() << current_channel << " set channel key to " << authority << "\n"; }
				}
            }
            else {
                setSetupStatus(SubscriptionManager::e_disconnected);
								{FileLogger fl(program_name); fl.f() << "Failed to parse channel: " << buf << "\n"<<std::flush; }
                DBG_CHANNELS << " failed to parse: " << buf << "\n";
                current_channel = "";
            }
            cJSON_Delete(chan);
        }
    }
    if (current_channel == "") return false;
    return true;
}

void SubscriptionManager::configureSetupConnection(const char *host, int port) {
	setup_host = host; // TBD this has to be the same as the subscriber host
	setup_port = port;
}
    
static char *setup_url = 0;
bool SubscriptionManager::setupConnections() {
	char url[100];
	snprintf(url, 100, "tcp://%s:%d", subscriber_host.c_str(), setup_port);

	if (setupStatus() == SubscriptionManager::e_startup ) {

		int counter = 5;
		while (counter-- > 0 && !monit_setup->active()) { DBG_CHANNELS << "waiting.." << counter << "\n"; usleep(100); }
		if (!monit_setup->active()) {
			char buf[150];
			snprintf(buf, 150, "Connection monitor for %s failed to activate", url);
			std::cerr << buf << "\n";
			return false;
		}

		current_channel = "";
		try {
			{FileLogger fl(program_name); fl.f() << "SubscriptionManager connecting to " << url << "\n"<<std::flush; }
			if (setup_url) free(setup_url);
			setup_url = strdup(url);
			setup().connect(url);
		}
		catch(zmq::error_t err) {
			{
				char buf[200];
				snprintf(buf, 200, "%s %d %s %s %s\n",
						 "SubscriptionManager::setupConnections error ",
						 errno,
						 zmq_strerror(zmq_errno()),
						 " url: ",
						 url);

				FileLogger fl(program_name); fl.f() << buf << "\n";
				MessageLog::instance()->add(buf);
			}
			return false;
		}
		monit_setup->setEndPoint(url);
		setSetupStatus(SubscriptionManager::e_waiting_connect);
		usleep(1000);
	} 
	if (requestChannel()) {
		// define the channel
		snprintf(url, 100, "tcp://%s:%d", subscriber_host.c_str(), subscriber_port);
		if (!monit_subs.disconnected() && strcmp(url, channel_url.c_str()) != 0) {
			// perhaps the subscriber hasn't had time to notice loss of connection but
			// the url is now different. We can't deal with this situation yet
			std::cerr << "NOTE: Channel has changed from " << channel_url << " to " << url << " exiting\n";
			exit(0);
		}
		channel_url = url;
		if ( sub_status_ == ss_init && monit_subs.disconnected()) {
			DBG_CHANNELS << " connecting subscriber to " << channel_url << "\n";
			{FileLogger fl(program_name); fl.f() << " connecting subscriber to " << channel_url << "\n"; }
			monit_subs.setEndPoint(channel_url.c_str());
			int counter = 5;
			while (counter-- > 0 && !monit_subs.active()) usleep(100);
			if (!monit_subs.active()) {
				DBG_CHANNELS << channel_url << " monitor subscriber not active\n";
				return false;
			}
			subscriber().connect(channel_url.c_str());

			//setSetupStatus(SubscriptionManager::e_done); //TBD is this correct? Shouldn't be here
			sub_status_ =  (protocol == eCLOCKWORK) ? ss_sub : ss_ready;
		}
		return true;
	}
	return false;
}

void SubscriptionManager::setSetupStatus( Status new_status ) {
	assert(isClient());
	if (_setup_status != new_status) 
	{
		FileLogger fl(program_name); fl.f() 
			<< "setup status " << _setup_status << " -> " << new_status << "\n"<<std::flush;
		state_start = microsecs();
		_setup_status = new_status;
	}
}

bool SubscriptionManager::isClient() {
	return _setup_status != e_not_used;
}

void SubscriptionManager::setupSender() {
	assert(sender_ == 0);
	assert(protocol == eCHANNEL);
	char url[100];
	snprintf(url, 100, "tcp://*:%d", subscriber_port+1);
	sender_ = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PAIR);
	sender_->connect(url);
}

zmq::socket_t &SubscriptionManager::subscriber() { return subscriber_; }
zmq::socket_t &SubscriptionManager::setup() { assert(setup_); return *setup_; }
zmq::socket_t *SubscriptionManager::sender() { return sender_; }


static int channel_error_count = 0;

enum MessageSource {SRC_CW, SRC_CHANNEL };

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
	MessageRouterInternals() : remote(0),default_dest(0),done(false),
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
	if (!internals->remote) { usleep(10); return; }
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
		items[idx].events = ZMQ_POLLIN;
		items[idx].revents = 0;
		destinations[idx-1] = item.first;
		++idx;
	}

	try {
		int rc = zmq::poll(items, num_socks, 2);
		if (rc == 0) { return; }
	}
	catch (zmq::error_t zex) {
		{FileLogger fl(program_name);
			fl.f() << "MessageRouter zmq error " << zmq_strerror(zmq_errno()) << "\n";
		}
		if (zmq_errno() == EINTR) { return; }
		assert(false);
	}

	int c = 0;
	for (unsigned int i=0; i<num_socks; ++i) if (items[i].revents & ZMQ_POLLIN)++c;
	if (!c) return;

#if 0
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
					if ( filter->filter(&buf, len, mh ) ) {
						mh.start_time = microsecs();
						safeSend(*dest, buf, len, mh);
					}
				}
				else {
					DBG_CHANNELS << "forwarding " << buf << " to " << mh.dest << "\n";
					mh.start_time = microsecs();
					safeSend(*dest, buf, len, mh);
				}
			}
			else if (internals->default_dest) {
				mh.start_time = microsecs();
				safeSend(*internals->default_dest, buf, len);
			}
#if 1
			else {
				DBG_CHANNELS << "Message " << buf << " needed a default route but none has been set\n";
			}
#endif
		}
	}

	// forwarding to remote socket
	for (unsigned int i=1; i<num_socks; ++i) {
		if (items[i].revents & ZMQ_POLLIN) {
			DBG_CHANNELS << "activity on pollidx " << i << " route " << destinations[i-1] <<  "\n";
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
					//char disp[2*len+1];
					//for (int i=0; i<len; ++i) sprintf(disp+2*i, "%x", buf[i]);
					//disp[2*len] = 0;
					//DBG_CHANNELS << "Sending '" << disp << "'\n";
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

bool SubscriptionManager::checkConnections() {
	if (!isClient())
		return !monit_subs.disconnected();

	if (setupStatus() == e_startup) {
		setupConnections();
		return false;
	}

	if (state_start == 0) state_start = microsecs();

	if (monit_setup->disconnected()
			&& setupStatus() != e_startup && setupStatus() != e_waiting_connect) {
		setSetupStatus(e_waiting_connect);
		return false;
	}
	if (monit_setup->disconnected() || monit_subs.disconnected() )
	{
#if 1
		/*
		 FileLogger fl(program_name); fl.f()
			<<  ( ( monit_setup->disconnected() ) ? "setup down " : "setup up " )
			<< ( ( monit_subs.disconnected() ) ? "subs down " : "subs up " ) << "\n";
		 */
		uint64_t state_timeout = 0;
		switch (setupStatus()) {
			case e_disconnected:
			case e_done:
			case e_not_used:
				break;
			case e_waiting_connect: // allow one minute to connect to the server before restarting
				state_timeout = 60000000;
				break;
			case e_settingup_subscriber:
			case e_startup:
			case e_waiting_setup:
			case e_waiting_subscriber:
				state_timeout = 10000000;
		}
		if ( state_timeout && (this->protocol == eCLOCKWORK || this->protocol == eCHANNEL)
				&& microsecs() - state_start > state_timeout) {
			{FileLogger fl(program_name); fl.f() << " waiting too long in state " << setupStatus() << ". aborting\n"; }
			usleep(5);
			setSetupStatus(e_startup);
		}
#endif
		if (monit_setup->disconnected() && monit_subs.disconnected()) return false;
	}
	if (monit_setup->disconnected()) {
		FileLogger fl(program_name); fl.f() << "SubscriptionManager disconnected from server clockwork\n";
		usleep(100);
		return false;
	}

	if (monit_subs.disconnected() && !monit_setup->disconnected()) {
#if 1
		{
			FileLogger fl(program_name); fl.f()
				<< "SubscriptionManager disconnected from server publisher with setup status: " << setupStatus() << "\n"<<std::flush;
		}
#endif
		setupConnections();
		usleep(50000);
		return false;
	}
	if (monit_setup && !monit_setup->disconnected() && !monit_subs.disconnected())
		setSetupStatus(SubscriptionManager::e_done);

	channel_error_count = 0;

	return true;
}

bool SubscriptionManager::checkConnections(zmq::pollitem_t items[], int num_items, zmq::socket_t &cmd) {
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	assert(pgn_rc == 0);

	if (!checkConnections() &&isClient()) {
		/*
		FileLogger fl(program_name); fl.f()
			<< "SubscriptionManager checkConnections() failed ";
			fl.f()<< subscriber_host<<":"<<subscriber_port<< "\n";
		 */
	}
    int rc = 0;

	/* client programs that are not yet connected only monitor their own command channel for activity
	 	and this is assumed to always be item 2 in the poll items(!) 
	 */
	try {
		if (isClient() && num_items>2 && (monit_subs.disconnected() || monit_setup->disconnected()) ) {
			int idx = -1;
			int i=0; while (i<num_items) if (items[i].socket == &cmd) { idx=i; break; } else ++i;
			if (idx != -1)
				rc = zmq::poll( &items[idx], 1, 5);
			else
				rc = 0;
		}
		else
			rc = zmq::poll(items, num_items, 5);
	}
	catch (zmq::error_t zex) {
		char buf[200];
		snprintf(buf, 200, "%s %s %d %s %s",
				 channel_name.c_str(),
				 "exception",
				 zmq_errno(),
				 zmq_strerror(zmq_errno()),
				 "polling connections");
		FileLogger fl(program_name); fl.f() << buf << "\n";
		MessageLog::instance()->add(buf);
		return false;
	}
    if (rc == 0) return true; // no sockets have messages

	char *buf;
    size_t msglen = 0;
#if 0
	if (items[0].revents & ZMQ_POLLIN) {
		DBG_CHANNELS << "have message activity from clockwork\n";
	}
	if (items[1].revents & ZMQ_POLLIN) {
		DBG_CHANNELS << "have message activity from publisher\n";
	}
#endif
	// yuk. the command socket is assumed to be the last item in the poll item list.

	// check the command socket to see if a message is coming in from the main thread
	// if it is we pass the command to the remote using either the subscriber socket
	// or the setup socket depending on the circumstances.
	int command_item = num_items - 1;
	if (items[command_item].revents & ZMQ_POLLERR) {
		DBG_CHANNELS << tnam << " SubscriptionManager detected error at index " << command_item << "\n";
	}
	else if (items[command_item].revents & ZMQ_POLLIN) {
		DBG_CHANNELS << tnam << " SubscriptionManager detected command at index " << command_item << "\n";
	}

	// Here we process an incoming message that we may need to forward on and
	// collect a response for.  If the message is forwarded we change state to
	// e_waiting_response and otherwise we send a nak back to the server and
	// stay in e_waiting_cmd
    if (run_status == e_waiting_cmd && items[command_item].revents & ZMQ_POLLIN) {
		if (safeRecv(cmd, &buf, &msglen, false, 0)) {
            DBG_CHANNELS << " got cmd: " << buf << " of len: " << msglen << " from main thread\n";
			// if we are a client, pass commands through the setup socket to the other end
			// of the channel.
			if (isClient()) {
				if (monit_setup && !monit_setup->disconnected()) {
					if (protocol != eCHANNEL) {
						if (setupStatus() == e_done) {
							//{FileLogger fl(program_name); fl.f() << "received " <<buf<< "to pass on and get response\n"<<std::flush; }
							setup().send(buf,msglen);
							run_status = e_waiting_response;
						}
						else {
							{FileLogger fl(program_name); fl.f() << "received " <<buf<< "to pass on and get response"
								<< " but the channel is not completely setup yet\n"<<std::flush; }
							safeSend(cmd, "failed", 6);
						}
					}
					else {
						{FileLogger fl(program_name); fl.f() << channel_url << "received " <<buf<< " to publish\n"<<std::flush; }
						DBG_CHANNELS << channel_url << " forwarding message to subscriber\n";
						subscriber().send(buf, msglen);
						safeSend(cmd, "sent", 4);
					}
				}
				else {
					safeSend(cmd, "failed", 6);
				}
			}
			else if (!monit_subs.disconnected()) {
				DBG_CHANNELS << " forwarding message "<<buf<<" to subscriber\n";
				subscriber().send(buf, strlen(buf));
				if (protocol == eCLOCKWORK) { // require a response
					{FileLogger fl(program_name); fl.f() << "forwarding " <<buf<< " to client and waiting response\n"<<std::flush; }
					run_status = e_waiting_response;
				}
				else {
					{FileLogger fl(program_name); fl.f() << "forwarding " <<buf<< " to client\n"<<std::flush; }
					safeSend(cmd, "sent", 4);
				}
			}
			else {
				safeSend(cmd, "failed", 6);
			}
        }
	}

	if (run_status == e_waiting_response && !isClient()) {
		assert(false); // is this ever called?
		cmd.send("ack",3);
		run_status = e_waiting_cmd;
		return true;
	}
	else if (isClient()) {
		if (run_status == e_waiting_response && monit_setup && monit_setup->disconnected() ) {
			const char *msg = "disconnected, attempting reconnect";
			cmd.send(msg, strlen(msg));
			run_status = e_waiting_cmd;
		}
		else if (items[0].revents & ZMQ_POLLIN) {
			if (run_status == e_waiting_response) {
#if 0
				{
					FileLogger fl(program_name);
					if (isClient()) {
						fl.f() << "incoming response from setup channel\n";
					}
					else {
						fl.f() << "incoming response from subscriber\n";
					}
				}
#endif
				DBG_CHANNELS << "incoming response\n";

				// note that items[0].socket is the correct socket to use. do we really
				// need this use of setup() and subscriber()?
				bool got_response =
					(isClient())
						? safeRecv(setup(), &buf, &msglen, false, 0)
						: safeRecv(subscriber(), &buf, &msglen, false, 0);
				if (got_response) {
					DBG_CHANNELS << " forwarding response " << buf << "\n";
					if (msglen)
						cmd.send(buf, msglen);
					else
						cmd.send("", 0);
					run_status = e_waiting_cmd;
				}
			}
			else if ( items[0].revents & ZMQ_POLLIN ) {
				char *buf;
				size_t len;
				bool res = safeRecv(setup(), &buf, &len, false, 0);
				if (res) {FileLogger fl(program_name); fl.f() << "Clockwork message '" << buf << "' was ignored\n";}
				{
					FileLogger fl(program_name);
					fl.f() << "incoming response from setup channel not already caught. run_status: " << run_status << "\n";
					//assert(false);
				}
				delete[] buf;
			}
		}
	}
    return true;
}

