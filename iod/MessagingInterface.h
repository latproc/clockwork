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
#include <zmq.hpp>
#include "Message.h"
#include "value.h"
#include "symboltable.h" 
#include "cJSON.h"

#include <boost/thread.hpp>
#include <sstream>
#include "MessageEncoding.h"

enum Protocol { eCLOCKWORK, eRAW, eZMQ };

class MessagingInterface : public Receiver {
public:
    MessagingInterface(int num_threads, int port, Protocol proto = eZMQ);
    MessagingInterface(std::string host, int port, Protocol proto = eZMQ);
    ~MessagingInterface();
    static zmq::context_t *getContext();
    static void setContext(zmq::context_t *);
    char *send(const char *msg);
    char *send(const Message &msg);
    bool send_raw(const char *msg);
    void setCurrent(MessagingInterface *mi) { current = mi; }
    static MessagingInterface *getCurrent();
    static MessagingInterface *create(std::string host, int port, Protocol proto = eZMQ);
    char *sendCommand(std::string cmd, std::list<Value> *params);
    char *sendCommand(std::string cmd, Value p1 = SymbolTable::Null,
                     Value p2 = SymbolTable::Null,
                     Value p3 = SymbolTable::Null);
    char *sendState(std::string cmd, std::string name, std::string state_name);
    
    //Receiver interface
    virtual bool receives(const Message&, Transmitter *t);
    virtual void handle(const Message&, Transmitter *from, bool needs_receipt = false );
    
private:
    static zmq::context_t *zmq_context;
    void connect();
    static MessagingInterface *current;
	Protocol protocol;
    zmq::socket_t *socket;
    static std::map<std::string, MessagingInterface *>interfaces;
    bool is_publisher;
    std::string url;
	int connection;
	std::string hostname;
	int port;
    pthread_t owner_thread;
};


class SocketMonitor : public zmq::monitor_t {
public:
    SocketMonitor(zmq::socket_t &s, const char *snam) : sock(s), disconnected_(true), socket_name(snam) {
    }
    void operator()() {
        monitor(sock, socket_name);
    }
    virtual void on_monitor_started() {
        std::cerr << "monitor started\n";
    }
    virtual void on_event_connected(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_connected " << addr_ << "\n";
        disconnected_ = false;
    }
    virtual void on_event_connect_delayed(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_connect_delayed " << addr_ << "\n"; }
    virtual void on_event_connect_retried(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_connect_retried " << addr_ << "\n";}
    virtual void on_event_listening(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_listening " << addr_ << "\n"; }
    virtual void on_event_bind_failed(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_bind_failed " << addr_ << "\n"; }
    virtual void on_event_accepted(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_accepted " << event_.value << " " << addr_ << "\n";
        disconnected_ = false;
    }
    virtual void on_event_accept_failed(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_accept_failed " << addr_ << "\n"; }
    virtual void on_event_closed(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_closed " << addr_ << "\n";}
    virtual void on_event_close_failed(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_close_failed " << addr_ << "\n";}
    virtual void on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_disconnected "<< event_.value << " "  << addr_ << "\n";
        disconnected_ = true;
    }
    virtual void on_event_unknown(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_unknown " << addr_ << "\n"; }
    
    bool disconnected() { return disconnected_; }
    
protected:
    zmq::socket_t &sock;
    bool disconnected_;
    const char *socket_name;
};

class SingleConnectionMonitor : public SocketMonitor {
public:
    SingleConnectionMonitor(zmq::socket_t &s, const char *snam)
    : SocketMonitor(s, snam) { }
    virtual void on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
        SocketMonitor::on_event_disconnected(event_, addr_);
        sock.disconnect(sock_addr.c_str());
    }
    void setEndPoint(const char *endpt) { sock_addr = endpt; }
private:
    std::string sock_addr;
    SingleConnectionMonitor(const SingleConnectionMonitor&);
    SingleConnectionMonitor &operator=(const SingleConnectionMonitor&);
};


class SubscriptionManager {
public:
    enum Status{e_waiting_cmd, e_waiting_response, e_startup, e_disconnected, e_waiting_connect,
        e_settingup_subscriber, e_waiting_subscriber, e_done };
    
    SubscriptionManager(const char *chname) :
    subscriber(*MessagingInterface::getContext(), ZMQ_SUB),
    setup(*MessagingInterface::getContext(), ZMQ_REQ),
    monit_subs(subscriber, "inproc://monitor.subs"),
    monit_setup(setup, "inproc://monitor.setup"),
    subscriber_host("localhost"), channel_name(chname)
    {
        init();
    }
    void init() {
        
        int res;
        res = zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "", 0);
        assert (res == 0);
        
        boost::thread subscriber_monitor(boost::ref(monit_subs));
        
        // client
        boost::thread setup_monitor(boost::ref(monit_setup));
        
        run_status = e_waiting_cmd;
        setup_status = e_startup;
    }
    
    bool requestChannel() {
        size_t len = 0;
        if (setup_status == SubscriptionManager::e_disconnected) {
            char *channel_setup = MessageEncoding::encodeCommand("CHANNEL", channel_name);
            len = setup.send(channel_setup, strlen(channel_setup));
            assert(len);
            setup_status = SubscriptionManager::e_waiting_connect;
        }
        if (setup_status == SubscriptionManager::e_waiting_connect){
            char buf[1000];
            while (true) {
                try {
                    len = setup.recv(buf, 1000);
                    if (len < 1000) buf[len] =0;
                    std::cerr << buf << "\n";
                    break;
                }
                catch (zmq::error_t e) {
                    if (errno == EINTR) continue;
                    std::cerr << zmq_strerror(errno);
                    return false;
                }
            }
            assert(len);
            setup_status = SubscriptionManager::e_settingup_subscriber;
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
                    setup_status = SubscriptionManager::e_disconnected;
                    std::cout << " failed to parse: " << buf << "\n";
                    current_channel = "";
                }
                cJSON_Delete(chan);
            }
        }
        if (current_channel == "") return false;
        return true;
    }
    
    bool setupSubscriptions() {
        std::stringstream ss;
        if (setup_status == SubscriptionManager::e_startup) {
            ss << "tcp://" << subscriber_host << ":5555"; // TBD no fixed port
            setup.connect(ss.str().c_str());
            monit_setup.setEndPoint(ss.str().c_str());
            setup_status = SubscriptionManager::e_disconnected;
            usleep(5000);
        }
        if (requestChannel()) {
            // define the channel
            ss.clear(); ss.str("");
            ss << "tcp://" << subscriber_host << ":" << subscriber_port;
            std::string channel_url = ss.str();
            std::cerr << "connecting to " << channel_url << "\n";
            monit_subs.setEndPoint(channel_url.c_str());
            subscriber.connect(channel_url.c_str());
            setup_status = SubscriptionManager::e_done;
            return true;
        }
        return false;
    }
    
    bool checkConnections() {
        if (monit_setup.disconnected() && monit_subs.disconnected()) {
            setup_status = e_startup;
            setupSubscriptions();
            usleep(50000);
            return false;
        }
        if (setup_status == e_disconnected || ( !monit_setup.disconnected() && monit_subs.disconnected() ) ) {
            // clockwork has disconnected
            setupSubscriptions();
            usleep(50000);
            return false;
        }
        if (monit_subs.disconnected() || monit_setup.disconnected())
            return false;
        return true;
    }
    
    bool checkConnections(zmq::pollitem_t items[], int num_items, zmq::socket_t &cmd) {
        int rc = 0;
        if (monit_setup.disconnected() && monit_subs.disconnected()) {
            setup_status = e_startup;
            setupSubscriptions();
            usleep(50000);
            return false;
        }
        if (setup_status == e_disconnected || ( monit_setup.disconnected() && monit_subs.disconnected() ) ) {
            // clockwork has disconnected
            setupSubscriptions();
            usleep(50000);
            return false;
        }
        if (monit_subs.disconnected() || monit_setup.disconnected())
            rc = zmq::poll( &items[num_items-3], num_items-2, 500);
        else
            rc = zmq::poll(&items[0], num_items, 500);
        
        char buf[1000];
        size_t msglen = 0;
        if (run_status == e_waiting_cmd && items[num_items-3].revents & ZMQ_POLLIN) {
            if ( (msglen = cmd.recv(buf, 1000, ZMQ_NOBLOCK)) != 0) {
                buf[msglen] = 0;
                std::cerr << "got cmd: " << buf << "\n";
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
            if ( (msglen = setup.recv(buf, 1000, ZMQ_NOBLOCK)) != 0) {
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
    zmq::socket_t subscriber;
    zmq::socket_t setup;
    SingleConnectionMonitor monit_subs;
    SingleConnectionMonitor monit_setup;
    Status setup_status;
    Status run_status;
    int subscriber_port;
    std::string current_channel;
    std::string subscriber_host;
    std::string channel_name;
};


#endif
