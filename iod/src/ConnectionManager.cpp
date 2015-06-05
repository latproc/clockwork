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
	NB_MSG << socket_name << "Accepted\n";
}

void SingleConnectionMonitor::on_event_connected(const zmq_event_t &event_, const char* addr_) {
	SocketMonitor::on_event_connected(event_, addr_);
	NB_MSG << socket_name << " Connected\n";
}

void SingleConnectionMonitor::on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
        SocketMonitor::on_event_disconnected(event_, addr_);
        sock.disconnect(sock_addr.c_str());
		NB_MSG << socket_name << " Disconnected\n";
    }

void SingleConnectionMonitor::setEndPoint(const char *endpt) { sock_addr = endpt; }


void MachineShadow::setProperty(const std::string prop, Value &val) {
    properties.add(prop, val, SymbolTable::ST_REPLACE);
}

Value &MachineShadow::getValue(const char *prop) {
    return properties.lookup(prop);
}

void MachineShadow::setState(const std::string new_state) {
    state = new_state;
}

ConnectionManager::ConnectionManager() : aborted(false) { }

void ConnectionManager::setProperty(std::string machine_name, std::string prop, Value val) {
    std::map<std::string, MachineShadow*>::iterator found = machines.find(machine_name);
    MachineShadow *machine = 0;
    if (found == machines.end())
        machine = new MachineShadow;
    else
        machine = (*found).second;
    machine->setProperty(prop, val);
}

void ConnectionManager::setState(std::string machine_name, std::string new_state) {
    std::map<std::string, MachineShadow*>::iterator found = machines.find(machine_name);
    MachineShadow *machine = 0;
    if (found == machines.end())
        machine = new MachineShadow;
    else
        machine = (*found).second;
    machine->setState(new_state);
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

SubscriptionManager::SubscriptionManager(const char *chname, Protocol proto, const char *remote_host, int remote_port) :
	subscriber(*MessagingInterface::getContext(), (proto == eCLOCKWORK)?ZMQ_SUB:ZMQ_PAIR),
	setup(*MessagingInterface::getContext(), ZMQ_REQ),
	subscriber_port(remote_port),
	subscriber_host(remote_host),
	channel_name(chname), protocol(proto),
	monit_subs(subscriber, constructAlphaNumericString("inproc://", chname, ".subs", "inproc://monitor.subs").c_str() ),
	monit_pubs(0),
	monit_setup(0)
{
	monit_setup = new SingleConnectionMonitor(setup, constructAlphaNumericString("inproc://", chname, ".setup", "inproc://monitor.setup").c_str() );
	init();
}

SubscriptionManager::~SubscriptionManager() {
	
}

void SubscriptionManager::setSetupMonitor(SingleConnectionMonitor *monitor) {
    if (monit_setup) delete monit_setup;
    monit_setup = monitor;
}

void SubscriptionManager::init() {
	if (protocol == eCLOCKWORK) { // start the subscriber if necessary
		int res;
		res = zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "", 0);
		assert (res == 0);
	}
    boost::thread subscriber_monitor(boost::ref(monit_subs));
    
    // client
    boost::thread setup_monitor(boost::ref(*monit_setup));
    
    run_status = e_waiting_cmd;
    setSetupStatus(e_startup);
}

int SubscriptionManager::configurePoll(zmq::pollitem_t *items) {
    items[0].socket = setup; items[0].events = ZMQ_POLLERR | ZMQ_POLLIN; items[0].fd = 0; items[0].revents = 0;
    items[1].socket = subscriber; items[0].events = ZMQ_POLLERR | ZMQ_POLLIN; items[0].fd = 0; items[0].revents = 0;
    return 2;
}

bool SubscriptionManager::requestChannel() {
    size_t len = 0;
    if (setupStatus() == SubscriptionManager::e_waiting_connect && !monit_setup->disconnected()) {
        NB_MSG << "Requesting channel " << channel_name << "\n";
        char *channel_setup = MessageEncoding::encodeCommand("CHANNEL", channel_name);
        len = setup.send(channel_setup, strlen(channel_setup));
        assert(len);
        setSetupStatus(SubscriptionManager::e_waiting_setup);
        return false;
    }
    if (setupStatus() == SubscriptionManager::e_waiting_setup && !monit_setup->disconnected()){
        char buf[1000];
        if (!safeRecv(setup, buf, 1000, false, len)) return false;
        if (len == 0) return false; // no data yet
        if (len < 1000) buf[len] =0;
        assert(len);
        NB_MSG << "Got channel " << buf << "\n";
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
                NB_MSG << " failed to parse: " << buf << "\n";
                current_channel = "";
            }
            cJSON_Delete(chan);
        }
    }
    if (current_channel == "") return false;
    return true;
}
    
bool SubscriptionManager::setupConnections() {
    std::stringstream ss;
    if (setupStatus() == SubscriptionManager::e_startup || setupStatus() == SubscriptionManager::e_disconnected) {
        ss << "tcp://" << subscriber_host << ":" << 5555;
        current_channel = "";
				try {
        	setup.connect(ss.str().c_str());
				}
				catch(zmq::error_t err) {
					NB_MSG << "SubscriptionManager::setupConnections error " << errno << ": " << zmq_strerror(errno) << "\n";
					return false;
				}
        monit_setup->setEndPoint(ss.str().c_str());
        setSetupStatus(SubscriptionManager::e_waiting_connect);
        usleep(5000);
    }
    if (requestChannel()) {
        // define the channel
        ss.clear(); ss.str("");
        ss << "tcp://" << subscriber_host << ":" << subscriber_port;
        std::string channel_url = ss.str();
        DBG_MSG << "connecting to " << channel_url << "\n";
        monit_subs.setEndPoint(channel_url.c_str());
        subscriber.connect(channel_url.c_str());
        setSetupStatus(SubscriptionManager::e_done);
        return true;
    }
    return false;
}

void SubscriptionManager::setSetupStatus( Status new_status ) {
    _setup_status = new_status;
    state_start = microsecs();
}
    
bool SubscriptionManager::checkConnections() {
    if (monit_setup->disconnected() && monit_subs.disconnected()) {
        //uint64_t now = microsecs();
        /*if (setupStatus() == e_waiting_connect && !setup.connected()) {
            //setup.disconnect(monit_setup->endPoint().c_str());
            setSetupStatus(e_startup);
        }
        else 
         */
        if (setupStatus() != e_waiting_connect && setupStatus() != e_disconnected) {
             setSetupStatus(e_startup);
             setupConnections();
         }
        usleep(500000);
        return false;
    }
    if (setupStatus() == e_disconnected || ( !monit_setup->disconnected() && monit_subs.disconnected() ) ) {
        // clockwork has disconnected
        setupConnections();
        usleep(50000);
        return false;
    }
    if (monit_subs.disconnected() || monit_setup->disconnected()) {
        usleep(50000);
        return false;
	}
    return true;
}

bool SubscriptionManager::checkConnections(zmq::pollitem_t items[], int num_items) {
	assert(false); // shouldn't be used yet
	if (!checkConnections()) return false;
	int rc = 0;
	if (monit_subs.disconnected() || monit_setup->disconnected())
		rc = zmq::poll( &items[2], num_items-2, 500);
	else
		rc = zmq::poll(&items[0], num_items, 500);
	if (!rc) return true;
	return false;
}

bool SubscriptionManager::checkConnections(zmq::pollitem_t items[], int num_items, zmq::socket_t &cmd) {
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	if (!checkConnections()) {
		return false;
	}
    int rc = 0;
    if (monit_subs.disconnected() || monit_setup->disconnected())
        rc = zmq::poll( &items[2], num_items-2, 500);
    else
        rc = zmq::poll(&items[0], num_items, 500);
    if (!rc) return true;
    char buf[1000];
    size_t msglen = 0;
	if (items[num_items-1].revents & ZMQ_POLLIN) {
		NB_MSG << tnam << " SubscriptionManager detected command\n";
	}
    if (run_status == e_waiting_cmd && items[num_items-1].revents & ZMQ_POLLIN) {
		size_t ll = cmd.recv(buf, 1000);
        /*if (safeRecv(cmd, buf, 1000, false, msglen)) {
            if (msglen == 1000) msglen--;
            buf[msglen] = 0;
            DBG_MSG << " got cmd: " << buf << "\n";
            if (!monit_setup->disconnected()) setup.send(buf,msglen);
            run_status = e_waiting_response;
        }*/
	} else {
		int x = 1;
	}
    if (run_status == e_waiting_response && monit_setup->disconnected()) {
        const char *msg = "disconnected, attempting reconnect";
        cmd.send(msg, strlen(msg));
        run_status = e_waiting_cmd;
    }
    else if (run_status == e_waiting_response && items[0].revents & ZMQ_POLLIN) {
		NB_MSG << "incoming response\n";
        if (safeRecv(setup, buf, 1000, false, msglen)) {
			NB_MSG << " forwarding response " << buf << "\n";
            if (msglen && msglen<1000) {
                cmd.send(buf, msglen);
            }
            else {
                cmd.send("error", 5);
            }
            run_status = e_waiting_cmd;
        }
    }
    return true;
}

