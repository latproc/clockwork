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

SingleConnectionMonitor::SingleConnectionMonitor(zmq::socket_t &s, const char *snam)
    : SocketMonitor(s, snam) { }

void SingleConnectionMonitor::on_event_accepted(const zmq_event_t &event_, const char* addr_) {
	SocketMonitor::on_event_accepted(event_, addr_);
	//DBG_CHANNELS << socket_name << "Accepted\n";
}

void SingleConnectionMonitor::on_event_connected(const zmq_event_t &event_, const char* addr_) {
	SocketMonitor::on_event_connected(event_, addr_);
	//DBG_CHANNELS << socket_name << " Connected\n";
}

void SingleConnectionMonitor::on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
		//DBG_CHANNELS << socket_name << " Disconnected\n";
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

ConnectionManager::ConnectionManager() : owner_thread(pthread_self()), aborted(false) { }

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
		if (isalnum(*p)) *q++ = *p; ++p;
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

SubscriptionManager::SubscriptionManager(const char *chname, ProtocolType proto,
										 const char *remote_host, int remote_port) :
		subscriber_port(remote_port),
		subscriber_host(remote_host),
		channel_name(chname), protocol(proto), setup_port(5555), 
		subscriber_(*MessagingInterface::getContext(), (proto == eCLOCKWORK)?ZMQ_SUB:ZMQ_PAIR),
		monit_subs(subscriber_,
				   constructAlphaNumericString("inproc://", chname, ".subs", "inproc://monitor.subs").c_str() ),
		monit_pubs(0), monit_setup(0),
		setup_(0),
		_setup_status(e_startup), sub_status_(ss_init)
{
	if (subscriber_host == "*") {
		_setup_status = e_not_used; // This is not a client
		assert(protocol == eCHANNEL);
		char url[100];
		snprintf(url, 100, "tcp://*:%d", subscriber_port);
		subscriber_.bind(url);
	}
	if (isClient()) {
		setup_ = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
		monit_setup = new SingleConnectionMonitor(*setup_, constructAlphaNumericString("inproc://", chname, ".setup", "inproc://monitor.setup").c_str() );
		setSetupStatus(e_startup);
	}
	init();
}

SubscriptionManager::~SubscriptionManager() {
	//DBG_CHANNELS << "warning: SubscriptionManager did not cleanup properly\n";
}

void SubscriptionManager::setSetupMonitor(SingleConnectionMonitor *monitor) {
	assert(isClient());
    if (monit_setup) delete monit_setup;
    monit_setup = monitor;
}

void SubscriptionManager::init() {
	if (protocol == eCLOCKWORK) { // start the subscriber if necessary
		int res;
		res = zmq_setsockopt (subscriber_, ZMQ_SUBSCRIBE, "", 0);
		assert (res == 0);
	}
    boost::thread subscriber_monitor(boost::ref(monit_subs));
	if (isClient()) boost::thread setup_monitor(boost::ref(*monit_setup));
	run_status = e_waiting_cmd;
}

int SubscriptionManager::configurePoll(zmq::pollitem_t *items) {
	int idx = 0;
	if (setup_) {
    	items[idx].socket = setup();
		items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;
		items[idx].fd = 0;
		items[idx].revents = 0;
		++idx;
	}
    items[idx].socket = subscriber();
	items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;
	items[idx].fd = 0;
	items[idx].revents = 0;
    return ++idx;
}

bool SubscriptionManager::requestChannel() {
    size_t len = 0;
	assert(isClient());
    if (setupStatus() == SubscriptionManager::e_waiting_connect && !monit_setup->disconnected()) {
			bool sent_request = false;
			int error_count = 0;
			while (!sent_request && error_count<4) {
				DBG_CHANNELS << "Requesting channel " << channel_name << "\n";
        char *channel_setup = MessageEncoding::encodeCommand("CHANNEL", channel_name);
				try {
        len = setup().send(channel_setup, strlen(channel_setup));
				if (len == 0) // temporarily unavailable
					usleep(20);
				else sent_request = true;
				}
				catch (zmq::error_t ex) {
					++error_count;
					{FileLogger fl(program_name); fl.f() << channel_name<< " exception " << zmq_errno()  << " "
						<< zmq_strerror(zmq_errno()) << " requesting channel\n"<<std::flush; }
					if (zmq_errno() == 156384763 /*EFSM*/) { 
						{FileLogger fl(program_name); fl.f() << channel_name << " attempting recovery requesting channels\n" << std::flush; }
						zmq::message_t m; setup().recv(&m); 
						{FileLogger fl(program_name); fl.f() << channel_name << " attempting recovery requesting channels\n" << std::flush; }
						return false;
					}
				}
			}
			if (error_count >=4) {
				{FileLogger fl(program_name); fl.f() << channel_name << " aborting\n" << std::flush; }
					exit(2);
			}
       setSetupStatus(SubscriptionManager::e_waiting_setup);
       return false;
    }
    if (setupStatus() == SubscriptionManager::e_waiting_setup && !monit_setup->disconnected()){
        char buf[1000];
        if (!safeRecv(setup(), buf, 1000, false, len, 2)) return false; // attempt a connection but do not wait very long before giving up
        if (len == 0) return false; // no data yet
        if (len < 1000) buf[len] =0;
        assert(len);
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
	// setupStatus() == SubscriptionManager::e_disconnected)  ????
	char url[100];
	if (setupStatus() == SubscriptionManager::e_startup ) {
		snprintf(url, 100, "tcp://%s:%d", subscriber_host.c_str(), setup_port);
		current_channel = "";
		try {
			{FileLogger fl(program_name); fl.f() << "SubscriptionManager connecting to " << url << "\n"<<std::flush; }
			setup_url = strdup(url);
			setup().connect(url);
		}
		catch(zmq::error_t err) {
			{FileLogger fl(program_name); fl.f() << 

				"SubscriptionManager::setupConnections error " << errno << ": " << zmq_strerror(errno) << " url: " << url << "\n"; }
			std::cerr << "SubscriptionManager::setupConnections error " << errno << ": " << zmq_strerror(errno) << " url: " << url << "\n";
			return false;
		}
		monit_setup->setEndPoint(url);
		setSetupStatus(SubscriptionManager::e_waiting_connect);
		usleep(50000);
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

zmq::socket_t &SubscriptionManager::subscriber() { return subscriber_; }
zmq::socket_t &SubscriptionManager::setup() { assert(setup_); return *setup_; }

static int channel_error_count = 0;

bool SubscriptionManager::checkConnections() {
	if (!isClient()) {
/*
		if (monit_subs.disconnected())
			{FileLogger fl(program_name); fl.f() << channel_name << "SubscriptionManager checkConnections() server has no client connection\n"<<std::flush; }
		else
			{FileLogger fl(program_name); fl.f() << channel_name << "SubscriptionManager checkConnections() server has client connection\n"<<std::flush;}
*/
		return !monit_subs.disconnected();
	}
	uint64_t timer = microsecs() - state_start;
	{
		if (setupStatus() != e_done) {
			FileLogger fl(program_name); fl.f() << channel_name 
				<< " state " << setupStatus() << " timer: " << timer << "\n"<<std::flush; 
		}
		if (timer>2000000 and (setupStatus() == e_waiting_connect || setupStatus() == e_disconnected) ) {
			{FileLogger fl(program_name); fl.f() << channel_name << "attempting connect\n"; }
			setSetupStatus(e_startup);
			setupConnections();
		}
		if (timer>2000000 and setupStatus() == e_waiting_setup) {
			{FileLogger fl(program_name); fl.f() << channel_name << "attempting connect\n"; }
			setSetupStatus(e_waiting_connect);
			setupConnections();
		}
	}
	


	if (monit_setup->disconnected() || monit_subs.disconnected() )
	{
		FileLogger fl(program_name); fl.f()
		<<  ( ( monit_setup->disconnected() ) ? "setup down " : "setup up " )
		<< ( ( monit_subs.disconnected() ) ? "subs down " : "subs up " ) << "\n";
	}

	if (monit_setup->disconnected() && setupStatus() == e_done) {
		setSetupStatus(e_startup);
		return false; // wait for reconnect
	}



	if (monit_setup->disconnected() && monit_subs.disconnected()) {
		if (setupStatus() != e_waiting_connect && setupStatus() != e_disconnected) {
			{FileLogger fl(program_name);
				fl.f() << channel_name << "SubscriptionManager checkConnections() attempting to setup connection "
					<< " setup status is " << setupStatus() << "\n" << std::flush; }
			if (setupStatus() == e_done || setupStatus() == e_settingup_subscriber) setSetupStatus(e_waiting_connect);
			//setSetupStatus(e_startup);
			setupConnections();
		}
		else
		{
			FileLogger fl(program_name); fl.f() 
				<< channel_name 
				<< "SubscriptionManager has no client or setup connection but setup status is " 
				<< setupStatus() << "\n" << std::flush;
			if (timer > 2000000) {
				try {
					if (!monit_setup->disconnected() )
						setup().disconnect(setup_url);
				}
				catch (std::exception ex) {
					FileLogger fl(program_name); fl.f() << channel_name << "exception "
					<< zmq_strerror(zmq_errno()) <<" disconnecting\n";
				}
				setSetupStatus(e_startup);
			}
		}
		usleep(50000);
		return false;
	}
	if (setupStatus() == e_disconnected) {
		{FileLogger fl(program_name); fl.f() << channel_name << "SubscriptionManager clockwork connection has broken..\n"; }
		setupConnections();
		usleep(50000);
		return false;
	}
    else if ( !monit_setup->disconnected() && monit_subs.disconnected() ) {
        // no subscriber
		{FileLogger fl(program_name); fl.f() << channel_name
					<< "SubscriptionManager subscriber is not connected clockwork connection state: "
					<< setupStatus() << "\n"; }
		if (setupStatus() == e_done) {
			{FileLogger fl(program_name); fl.f() << "Too many errors: exiting\n"; }
			++channel_error_count;
			if (channel_error_count>20) {
				exit(2);
			}
		}
        setupConnections();
        usleep(50000);
        return false;
    }
    if (monit_subs.disconnected() || monit_setup->disconnected()) {
				if (monit_subs.disconnected())
				{FileLogger fl(program_name); fl.f() << "SubscriptionManager disconnected from server publisher\n"<<std::flush; }
				if (monit_setup->disconnected())
				{FileLogger fl(program_name); fl.f() << "SubscriptionManager disconnected from server clockwork\n"<<std::flush; }
        usleep(50000);
        return false;
	}
	if (monit_setup && !monit_setup->disconnected())
		setSetupStatus(SubscriptionManager::e_done);

	channel_error_count = 0;
    return true;
}

bool SubscriptionManager::checkConnections(zmq::pollitem_t items[], int num_items) {
	assert(false); // shouldn't be used yet
	if (!checkConnections()) return false;
	int rc = 0;
	if (monit_subs.disconnected() || ( monit_setup && monit_setup->disconnected() ) )
		rc = zmq::poll( &items[2], num_items-2, 500);
	else
		rc = zmq::poll(&items[0], num_items, 500);
	if (!rc) return true;
	return false;
}

bool SubscriptionManager::checkConnections(zmq::pollitem_t items[], int num_items, zmq::socket_t &cmd) {
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	assert(pgn_rc == 0);

	if (!checkConnections()) {
		{FileLogger fl(program_name); fl.f() << "SubscriptionManager checkConnections() failed\n"<<std::flush; }
		return false;
	}
    int rc = 0;
    if (isClient() && num_items>2 && (monit_subs.disconnected() || monit_setup->disconnected()) )
        rc = zmq::poll( &items[2], num_items-2, 500);
    else
        rc = zmq::poll(items, num_items, 500);
    if (rc == 0) return true; // no sockets have messages

	char buf[1000]; // TBD BUG this should be allocated dynamically
    size_t msglen = 0;

	if (items[0].revents & ZMQ_POLLIN) {
		NB_MSG << "have message from clockwork\n";
	}
	if (items[1].revents & ZMQ_POLLIN) {
		NB_MSG << "have message from publisher\n";
	}

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

    if (run_status == e_waiting_cmd && items[command_item].revents & ZMQ_POLLIN) {
		if (safeRecv(cmd, buf, 1000, false, msglen)) {
            if (msglen == 1000) msglen--;
            buf[msglen] = 0;
            DBG_CHANNELS << " got cmd: " << buf << " from main thread\n";
			// if we are a client, pass commands through the setup socket to the other end
			// of the channel.
			if (isClient() && monit_setup && !monit_setup->disconnected()) {
				if (protocol != eCHANNEL) {
					//{FileLogger fl(program_name); fl.f() << "received " <<buf<< "to pass on and get response\n"<<std::flush; }
					setup().send(buf,msglen);
					run_status = e_waiting_response;
				}
				else {
					//{FileLogger fl(program_name); fl.f() << "received " <<buf<< "to publish\n"<<std::flush; }
					DBG_CHANNELS << " forwarding message to subscriber\n";
					subscriber().send(buf, msglen);
					safeSend(cmd, "sent", 4);
				}
			}
			else if (!monit_subs.disconnected()) {
				DBG_CHANNELS << " forwarding message "<<buf<<" to subscriber\n";
				subscriber().send(buf, msglen);
				if (protocol == eCLOCKWORK) { // require a response
					{FileLogger fl(program_name); fl.f() << "forwarding " <<buf<< "to client and waiting response\n"<<std::flush; }
					run_status = e_waiting_response;
				}
				else {
					{FileLogger fl(program_name); fl.f() << "forwarding " <<buf<< "to client\n"<<std::flush; }
					safeSend(cmd, "sent", 4);
				}
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
		else if (run_status == e_waiting_response && items[0].revents & ZMQ_POLLIN) {
			//DBG_CHANNELS << "incoming response\n";
			bool got_response =
				(isClient())
					? safeRecv(setup(), buf, 1000, false, msglen)
					: safeRecv(subscriber(), buf, 1000, false, msglen);
			if (got_response) {
				//DBG_CHANNELS << " forwarding response " << buf << "\n";
				if (msglen && msglen<1000) {
					cmd.send(buf, msglen);
				}
				else {
					cmd.send("error", 5);
				}
				run_status = e_waiting_cmd;
			}
		}
	}
    return true;
}

