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

MessagingInterface *MessagingInterface::current = 0;
zmq::context_t *MessagingInterface::zmq_context = 0;
std::map<std::string, MessagingInterface *>MessagingInterface::interfaces;

zmq::context_t *MessagingInterface::getContext() { return zmq_context; }

bool safeRecv(zmq::socket_t &sock, char *buf, int buflen, bool block, size_t &response_len, uint64_t timeout) {
//    struct timeval now;
//    gettimeofday(&now, 0);
//    uint64_t when = now.tv_sec * 1000000L + now.tv_usec + timeout;
    
    response_len = 0;
    while (true) {
        try {
                zmq::pollitem_t items[] = { { sock, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 } };
                int n = zmq::poll( &items[0], 1, timeout);
								if (!n && block) continue;
                if (items[0].revents & ZMQ_POLLIN) {
     	            response_len = sock.recv(buf, buflen);
                if (!response_len && block) continue;
            }
            return (response_len == 0) ? false : true;
        }
        catch (zmq::error_t e) {
            if (errno == EINTR) continue;
            std::cerr << zmq_strerror(errno) << "\n";
            return false;
        }
    }
}

bool sendMessage(const char *msg, zmq::socket_t &sock, std::string &response) {
    while (1) {
        try {
            std::cout << "sent " << msg << "\n";
            size_t len = sock.send(msg, strlen(msg));
            if (!len) continue;
            break;
        }
        catch (zmq::error_t e) {
            if (errno == EINTR) continue;
            std::cout << "sendMessage: " << zmq_strerror(errno) << "\n";
            return false;
        }
    }
    char buf[200];
    size_t len = 0;
    while (len == 0) {
        try {
            len = sock.recv(buf, 200);
            if (!len) continue;
            if (len==200) len--; //unlikely event that the response is large
            buf[len] = 0;
            std::cerr << "received: " << buf << "\n";
            response = buf;
            break;
        }
        catch(zmq::error_t e)  {
            if (errno == EINTR) continue;
            char err[300];
            snprintf(err, 300, "error: %s\n", zmq_strerror(errno));
            std::cerr << err;
            return false;
        }
    }
    return true;
}

void MessagingInterface::setContext(zmq::context_t *ctx) {
    zmq_context = ctx;
    assert(zmq_context);
}
/*
MessagingInterface *MessagingInterface::getCurrent() {
    if (MessagingInterface::current == 0) {
        MessagingInterface::current = new MessagingInterface(1, publisher_port());
        usleep(200000); // give current subscribers time to notice us
    }
    return MessagingInterface::current;
}
 */

MessagingInterface *MessagingInterface::create(std::string host, int port, Protocol proto) {
    std::stringstream ss;
    ss << host << ":" << port;
    std::string id = ss.str();
    if (interfaces.count(id) == 0) {
        MessagingInterface *res = new MessagingInterface(host, port, proto);
        interfaces[id] = res;
        return res;
    }
    else
        return interfaces[id];
}

MessagingInterface::MessagingInterface(int num_threads, int port, Protocol proto) 
		: Receiver("messaging_interface"), protocol(proto), socket(0),is_publisher(false), connection(-1) {
    owner_thread = pthread_self();
	if (protocol == eCLOCKWORK || protocol == eZMQ) {
	    socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PUB);
	    is_publisher = true;
	    std::stringstream ss;
	    ss << "tcp://*:" << port;
	    url = ss.str();
        socket->bind(url.c_str());
	}
	else {
		connect();
	}
}

MessagingInterface::MessagingInterface(std::string host, int remote_port, Protocol proto) 
		:Receiver("messaging_interface"), protocol(proto), socket(0),is_publisher(false),
            connection(-1), hostname(host), port(remote_port), owner_thread(0) {
	if (protocol == eCLOCKWORK || protocol == eZMQ) {
        owner_thread = pthread_self();
	    if (host == "*") {
	        socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PUB);
	        is_publisher = true;
	        std::stringstream ss;
	        ss << "tcp://*:" << port;
	        url = ss.str();
	        socket->bind(url.c_str());
	    }
	    else {
            if (protocol == eCLOCKWORK || protocol == eZMQ)
                socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
	        std::stringstream ss;
	        ss << "tcp://" << host << ":" << port;
	        url = ss.str();
	        connect();
	    }
	}
	else {
		connect();
	}
}

void MessagingInterface::connect() {
	if (protocol == eCLOCKWORK || protocol == eZMQ) {
        assert( pthread_equal(owner_thread, pthread_self()) );
	    is_publisher = false;
	    socket->connect(url.c_str());
	    int linger = 0;
	    socket->setsockopt (ZMQ_LINGER, &linger, sizeof (linger));
	}
	else {
		char error[ANET_ERR_LEN];
    	connection = anetTcpConnect(error, hostname.c_str(), port);
    	if (connection == -1) {
        	MessageLog::instance()->add(error);
    	    std::cerr << error << "\n";
    	}
	}
}

MessagingInterface::~MessagingInterface() {
	int retries = 3;
	while  (retries-- && connection != -1) { 
		int err = close(connection);
		if (err == -1 && errno == EINTR) continue;
		connection = -1;
	}
    if (MessagingInterface::current == this) MessagingInterface::current = 0;
    socket->disconnect(url.c_str());
    delete socket;
}

bool MessagingInterface::receives(const Message&, Transmitter *t) {
    return true;
}
void MessagingInterface::handle(const Message&msg, Transmitter *from, bool needs_receipt ) {
    char *response = send(msg);
    if (response)
        free(response);
}


char *MessagingInterface::send(const char *txt) {
    //if (owner_thread) assert( pthread_equal(owner_thread, pthread_self()) );

    if (!is_publisher){
        DBG_MESSAGING << "sending message " << txt << " on " << url << "\n";
    }
    else {
        DBG_MESSAGING << "sending message " << txt << "\n";
    }
    size_t len = strlen(txt);
    
    // We try to send a few times and if an exception occurs we try a reconnect
    // but is this useful without a sleep in between?
    // It seems unlikely the conditions will have changed between attempts.
    while (true) {
        try {
            zmq::message_t msg(len);
            strncpy ((char *) msg.data(), txt, len);
            socket->send(msg);
            break;
        }
        catch (std::exception e) {
            if (errno == EINTR) continue;
            if (zmq_errno())
                std::cerr << "Exception when sending " << url << ": " << zmq_strerror(zmq_errno()) << "\n";
            else
                std::cerr << "Exception when sending " << url << ": " << e.what() << "\n";
            socket->disconnect(url.c_str());
            usleep(50000);
            connect();
        }
    }
    if (!is_publisher) {
        while (true) {
            try {
                zmq::pollitem_t items[] = { { *socket, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 } };
                zmq::poll( &items[0], 1, 500);
                if (items[0].revents & ZMQ_POLLIN) {
                    zmq::message_t reply;
                    if (socket->recv(&reply)) {
                        len = reply.size();
                        char *data = (char *)malloc(len+1);
                        memcpy(data, reply.data(), len);
                        data[len] = 0;
                        std::cout << url << ": " << data << "\n";
                        return data;
                    }
                }
                else if (items[0].revents & ZMQ_POLLERR) {
                    std::cerr << "MessagingInterface::send: error during recv\n";
                    continue;
                }
                std::cerr << "timeout: abandoning message " << txt << "\n";
                socket->disconnect(url.c_str());
                delete socket;
                socket = new zmq::socket_t(*getContext(), ZMQ_REQ);
                usleep(50000);
                connect();
                break;
            }
            catch (std::exception e) {
                if (zmq_errno())
                    std::cerr << "Exception when receiving response " << url << ": " << zmq_strerror(zmq_errno()) << "\n";
                else
                    std::cerr << "Exception when receiving response " << url << ": " << e.what() << "\n";
            }
        }
    }
    return 0;
}

char *MessagingInterface::send(const Message&msg) {
    char *text = MessageEncoding::encodeCommand(msg.getText(), msg.getParams());
    char *res = send(text);
    free(text);
    return res;
}

bool MessagingInterface::send_raw(const char *msg) {
    char error[ANET_ERR_LEN];
	int retry=2;
    size_t len = strlen(msg);
	while (retry--) {
	    size_t written = anetWrite(connection, (char *)msg, len);
	    if (written != len) {
	        snprintf(error, ANET_ERR_LEN, "error sending message: %s", msg );
	        MessageLog::instance()->add(error);
			close(connection);
			connect();
	    }
		else return true;
	}
	if (!retry) return false;
    return true;
}
char *MessagingInterface::sendCommand(std::string cmd, std::list<Value> *params) {
    char *request = MessageEncoding::encodeCommand(cmd, params);
    char *response = send(request);
    free(request);
    return response;
}

/* TBD, this may need to be optimised, one approach would be to use a 
 templating system; a property message looks like this:
 
 { "command":    "PROPERTY", "params":   
    [
         { "type":  "NAME", "value":    $1 },
         { "type":   "NAME", "value":   $2 },
         { "type":  TYPEOF($3), "value":  $3 }
     ] }
 
 */

char *MessagingInterface::sendCommand(std::string cmd, Value p1, Value p2, Value p3)
{
    std::list<Value>params;
    params.push_back(p1);
    params.push_back(p2);
    params.push_back(p3);
    return sendCommand(cmd, &params);
}

char *MessagingInterface::sendState(std::string cmd, std::string name, std::string state_name)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "command", cmd.c_str());
    cJSON *cjParams = cJSON_CreateArray();
    cJSON_AddStringToObject(cjParams, "name", name.c_str());
    cJSON_AddStringToObject(cjParams, "state", state_name.c_str());
    cJSON_AddItemToObject(msg, "params", cjParams);
    char *request = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    char *response = send(request);
    free (request);
    return response;
}

    SocketMonitor::SocketMonitor(zmq::socket_t &s, const char *snam) : sock(s), disconnected_(true), socket_name(snam) {
    }
void SocketMonitor::operator()() {
		char thread_name[100];
		snprintf(thread_name, 100, "iod skt monitor %s", socket_name);
	    pthread_setname_np(pthread_self(), thread_name);

        monitor(sock, socket_name);
    }
void SocketMonitor::on_monitor_started() {
        //DBG_MSG << "monitor started\n";
    }
void SocketMonitor::on_event_connected(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_connected " << addr_ << "\n";
        disconnected_ = false;
    }
void SocketMonitor::on_event_connect_delayed(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_connect_delayed " << addr_ << "\n";
}
void SocketMonitor::on_event_connect_retried(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_connect_retried " << addr_ << "\n";
}
void SocketMonitor::on_event_listening(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_listening " << addr_ << "\n";
}
void SocketMonitor::on_event_bind_failed(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_bind_failed " << addr_ << "\n";
}
void SocketMonitor::on_event_accepted(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_accepted " << event_.value << " " << addr_ << "\n";
        disconnected_ = false;
    }
void SocketMonitor::on_event_accept_failed(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_accept_failed " << addr_ << "\n";
}
void SocketMonitor::on_event_closed(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_closed " << addr_ << "\n";
}
void SocketMonitor::on_event_close_failed(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_close_failed " << addr_ << "\n";
}
void SocketMonitor::on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_disconnected "<< event_.value << " "  << addr_ << "\n";
        disconnected_ = true;
    }
void SocketMonitor::on_event_unknown(const zmq_event_t &event_, const char* addr_) {
        //DBG_MSG << "on_event_unknown " << addr_ << "\n";
}
    
bool SocketMonitor::disconnected() { return disconnected_;}


SingleConnectionMonitor::SingleConnectionMonitor(zmq::socket_t &s, const char *snam)
    : SocketMonitor(s, snam) { }
void SingleConnectionMonitor::on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
        SocketMonitor::on_event_disconnected(event_, addr_);
        sock.disconnect(sock_addr.c_str());
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

SubscriptionManager::SubscriptionManager(const char *chname, const char *remote_host, int remote_port) :
        publisher(0),
        subscriber(*MessagingInterface::getContext(), ZMQ_SUB),
        setup(*MessagingInterface::getContext(), ZMQ_REQ),
        subscriber_port(remote_port),
        subscriber_host(remote_host),
        channel_name(chname),
        monit_subs(subscriber, "inproc://monitor.subs"),
        monit_pubs(0),
        monit_setup(setup, "inproc://monitor.setup") {
    init();
}

void SubscriptionManager::init() {
    
    int res;
    res = zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "", 0);
    assert (res == 0);
    
    boost::thread subscriber_monitor(boost::ref(monit_subs));
    
    // client
    boost::thread setup_monitor(boost::ref(monit_setup));
    
    run_status = e_waiting_cmd;
    setSetupStatus(e_startup);
}

/*
void SubscriptionManager::usePublisher() {
    publisher = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PUB);
    monit_pubs = new SingleConnectionMonitor(*publisher, "inproc://monitor.pubs");
    int port = Channel::uniquePort();
    char url[100];
    snprintf(url, 100, "tcp:// *:%d/", port);
    publisher->bind(url);
}
 */

int SubscriptionManager::configurePoll(zmq::pollitem_t *items) {
    items[0].socket = setup; items[0].events = ZMQ_POLLERR | ZMQ_POLLIN; items[0].fd = 0; items[0].revents = 0;
    items[1].socket = subscriber; items[0].events = ZMQ_POLLERR | ZMQ_POLLIN; items[0].fd = 0; items[0].revents = 0;
    return 2;
}

bool SubscriptionManager::requestChannel() {
    size_t len = 0;
    if (setupStatus() == SubscriptionManager::e_waiting_connect && !monit_setup.disconnected()) {
        std::cout << "Requesting channel " << channel_name << "\n";
        char *channel_setup = MessageEncoding::encodeCommand("CHANNEL", channel_name);
        len = setup.send(channel_setup, strlen(channel_setup));
        assert(len);
        setSetupStatus(SubscriptionManager::e_waiting_setup);
        return false;
    }
    if (setupStatus() == SubscriptionManager::e_waiting_setup){
        char buf[1000];
        if (!safeRecv(setup, buf, 1000, false, len)) return false;
        if (len == 0) return false; // no data yet
        if (len < 1000) buf[len] =0;
        assert(len);
        std::cout << "Got channel " << buf << "\n";
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
                std::cout << " failed to parse: " << buf << "\n";
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
        ss << "tcp://" << subscriber_host << ":" << subscriber_port;
        setup.connect(ss.str().c_str());
        monit_setup.setEndPoint(ss.str().c_str());
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
    if (monit_setup.disconnected() && monit_subs.disconnected()) {
        //uint64_t now = microsecs();
        /*if (setupStatus() == e_waiting_connect && !setup.connected()) {
            //setup.disconnect(monit_setup.endPoint().c_str());
            setSetupStatus(e_startup);
        }
        else 
         */if (setupStatus() != e_waiting_connect && setupStatus() != e_disconnected) {
             setSetupStatus(e_startup);
             setupConnections();
         }
        usleep(50000);
        return false;
    }
    if (setupStatus() == e_disconnected || ( !monit_setup.disconnected() && monit_subs.disconnected() ) ) {
        // clockwork has disconnected
        setupConnections();
        usleep(50000);
        return false;
    }
    if (monit_subs.disconnected() || monit_setup.disconnected())
        return false;
    return true;
}
    
bool SubscriptionManager::checkConnections(zmq::pollitem_t items[], int num_items, zmq::socket_t &cmd) {
    if (!checkConnections()) return false;
    int rc = 0;
    if (monit_subs.disconnected() || monit_setup.disconnected())
        rc = zmq::poll( &items[num_items-3], num_items-2, 500);
    else
        rc = zmq::poll(&items[0], num_items, 500);
    if (!rc) return true;
    
    char buf[1000];
    size_t msglen = 0;
    if (run_status == e_waiting_cmd && items[num_items-3].revents & ZMQ_POLLIN) {
        if (safeRecv(cmd, buf, 1000, false, msglen)) {
            if (msglen == 1000) msglen--;
            buf[msglen] = 0;
            DBG_MSG << "got cmd: " << buf << "\n";
            if (!monit_setup.disconnected()) setup.send(buf,msglen);
            run_status = e_waiting_response;
        }
    }
    if (run_status == e_waiting_response && monit_setup.disconnected()) {
        const char *msg = "disconnected, attempting reconnect";
        cmd.send(msg, strlen(msg));
        run_status = e_waiting_cmd;
    }
    else if (run_status == e_waiting_response && items[0].revents & ZMQ_POLLIN) {
        if (safeRecv(setup, buf, 1000, false, msglen)) {
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

bool CommandManager::setupConnections() {
    if (setup_status == e_startup) {
        char url[100];
        snprintf(url, 100, "tcp://%s:5555", host_name.c_str()); // TBD no fixed port
        setup.connect(url);
        monit_setup.setEndPoint(url);
        setup_status = e_waiting_connect;
        usleep(5000);
    }
    return false;
}

bool CommandManager::checkConnections() {
    if (monit_setup.disconnected() ) {
        setup_status = e_startup;
        setupConnections();
        usleep(50000);
        return false;
    }
    if (monit_setup.disconnected())
        return false;
    return true;
}

bool CommandManager::checkConnections(zmq::pollitem_t *items, int num_items, zmq::socket_t &cmd) {
    int rc = 0;
    if (monit_setup.disconnected() ) {
        setup_status = e_startup;
        setupConnections();
        usleep(50000);
        return false;
    }
    if ( monit_setup.disconnected() )
        rc = zmq::poll( &items[1], num_items-1, 500);
    else
        rc = zmq::poll(&items[0], num_items, 500);
    
    char buf[1000];
    size_t msglen = 0;
    if (run_status == e_waiting_cmd && items[1].revents & ZMQ_POLLIN) {
        safeRecv(cmd, buf, 1000, false, msglen);
        if ( msglen ) {
            buf[msglen] = 0;
            DBG_MSG << "got cmd: " << buf << "\n";
            if (!monit_setup.disconnected()) setup.send(buf,msglen);
            run_status = e_waiting_response;
        }
    }
    if (run_status == e_waiting_response && monit_setup.disconnected()) {
        const char *msg = "disconnected, attempting reconnect";
        cmd.send(msg, strlen(msg));
        run_status = e_waiting_cmd;
    }
    else if (run_status == e_waiting_response && items[0].revents & ZMQ_POLLIN) {
        if (safeRecv(setup, buf, 1000, false, msglen)) {
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

CommandManager::CommandManager(const char *host) :
    host_name(host),
    setup(*MessagingInterface::getContext(), ZMQ_REQ),
    monit_setup(setup, "inproc://monitor.setup") {
    init();
}

void CommandManager::init() {
    // client
    boost::thread setup_monitor(boost::ref(monit_setup));
    
    run_status = e_waiting_cmd;
    setup_status = e_startup;
}

