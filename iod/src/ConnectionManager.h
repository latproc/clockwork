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

#include "Message.h"
#include "MessagingInterface.h"
#include "cJSON.h"
#include "rate.h"
#include "symboltable.h"
#include "value.h"
#include <boost/thread.hpp>
#include <map>
#include <pthread.h>
#include <set>
#include <sstream>
#include <string>
#include <zmq.hpp>

#include "MessageEncoding.h"
#include "SocketMonitor.h"

std::string constructAlphaNumericString(const char *prefix, const char *val, const char *suffix,
                                        const char *default_name);

class CommunicationPoll {
  public:
    CommunicationPoll *instance() {
        if (!instance_) {
            instance_ = new CommunicationPoll;
        }
        return instance_;
    }

  private:
    CommunicationPoll *instance_;
    CommunicationPoll();
    CommunicationPoll(const CommunicationPoll &);
    CommunicationPoll &operator=(const CommunicationPoll &);
};

class SingleConnectionMonitor : public SocketMonitor {
  public:
    SingleConnectionMonitor(zmq::socket_t &s);
    virtual void on_event_accepted(const zmq_event_t &event_, const char *addr_);
    virtual void on_event_disconnected(const zmq_event_t &event_, const char *addr_);
    virtual void on_event_connected(const zmq_event_t &event_, const char *addr_);
    void setEndPoint(const char *endpt);
    std::string &endPoint() { return sock_addr; }
    const std::string &socketName() const { return socket_name; }

  private:
    std::string socket_name;
    std::string sock_addr;
    SingleConnectionMonitor(const SingleConnectionMonitor &);
    SingleConnectionMonitor &operator=(const SingleConnectionMonitor &);
};

class MachineShadow {
    SymbolTable properties;
    std::set<std::string> states;
    std::string state;

  public:
    void setProperty(const std::string, Value &val);
    const Value &getValue(const char *prop);
    void setState(const std::string);
};

class ConnectionManagerInterface {
  public:
    virtual ~ConnectionManagerInterface() = default;
    virtual bool setupConnections() = 0;
    virtual bool checkConnections() = 0;
    virtual bool checkConnections(zmq::pollitem_t *items, int num_items, zmq::socket_t &cmd) = 0;
    virtual int numSocks() = 0;
    virtual bool ready() = 0;
};

class ConnectionManager : public ConnectionManagerInterface {
  public:
    ConnectionManager();
    virtual ~ConnectionManager();
    void abort();
    void aborted();
    bool ready() { return rate_limiter.ready(); }
    const pthread_t &ownerThread() const { return owner_thread; }

  protected:
    pthread_t owner_thread;
    bool has_aborted;
    std::map<std::string, MachineShadow *> machines;
    RateLimiter rate_limiter;
};

class MessageFilterInternals {
  public:
    MessageFilterInternals() {}
    virtual ~MessageFilterInternals() {}
};

class MessageFilter {
  public:
    MessageFilter() : internals(0) {}
    ~MessageFilter() {}
    virtual void init(MessageFilterInternals *) {}
    virtual bool filter(char **buf, size_t &len) { return true; }
    virtual bool filter(char **buf, size_t &len, MessageHeader &) { return true; }

  private:
    MessageFilter(const MessageFilter &);
    MessageFilter &operator=(const MessageFilter &);
    MessageFilterInternals *internals;
};

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

/*
    Subscription Manager - create and maintain a connection to a remote clockwork driver

    The command socket is used to request a channel and a subscriber is created to
    listen for activity from the server. The main thread can communicate with the
    server via a request/reply connection to a thread running the subscription manager.

    Commands arriving from the program's main thread
    are forwarded remote driver through the command socket.

*/
class SubscriptionManagerInternals;
class SubscriptionManager : public ConnectionManager {
  public:
    // RunStatus tracks the state of the connection between the main thread
    // and the subscription manager; this connection enables the main thread
    // to communicate with the remote application
    enum RunStatus { e_waiting_cmd, e_waiting_response };

    // Status tracks the state of the connection between the subscription
    // manager and the command interface of the remote application
    enum Status {
        e_not_used,
        e_startup,
        e_disconnected,
        e_waiting_connect,
        e_settingup_subscriber,
        e_waiting_subscriber,
        e_waiting_setup,
        e_done,
        e_error,
        e_connected
    };

    // SubStatus tracks the state of the connection between each of the subscription
    // manager's subscription channel; this is the main data channel for a subscription.
    // If one end of the channel is a publisher, sending data using a ZMQ PUB socket,
    // the other end is a subscriber and no protocol is used; the status of the
    // ends are set to ss_pub or ss_sub.
    // If the connection is a channel, both ends use a ZMQ PAIR socket and both ends
    // can arbitrarily send messages.
    enum SubStatus { ss_pub, ss_sub, ss_init, ss_ready };

    SubscriptionManager(const char *chname, ProtocolType proto = eCHANNEL,
                        const char *remote_host = "localhost", int remote_port = 5555,
                        int setup_port = 5555);
    virtual ~SubscriptionManager();
    void setSetupMonitor(SingleConnectionMonitor *monitor);
    void createSubscriberSocket(const char *chame);
    void init();

    bool requestChannel();

    void configureSetupConnection(const char *host, int port);

    bool setupConnections();
    bool checkConnections(); // test whether setup and subscriber channels are connected

    // the following variants poll for activity and handle commands arriving
    // on the command channel or pass them to the remote party through the subscriber or setup channel
    bool checkConnections(zmq::pollitem_t *items, int num_items, zmq::socket_t &cmd);
    bool checkConnections(zmq::pollitem_t *items, int num_items);
    virtual int numSocks() { return 2; }
    int configurePoll(zmq::pollitem_t *);

    zmq::socket_t &subscriber();
    zmq::socket_t *sender();
    void setupSender();
    zmq::socket_t &setup(); // invalid for non-client instances
    bool isClient();        // only clients have a setup socket

    Status setupStatus() const {
        return _setup_status;
    } // always e_not_used for non client instances
    void setSetupStatus(Status new_status);
    SubStatus subscriberStatus();
    uint64_t state_start;
    RunStatus run_status;
    std::string current_channel;
    std::string subscriber_host;
    std::string channel_name;
    std::string channel_url;
    ProtocolType protocol;
    std::string setup_host;
    int setup_port;
    uint64_t authority;

  protected:
    zmq::socket_t subscriber_;
    zmq::socket_t *sender_;
    int subscriber_port;

  public:
    SingleConnectionMonitor monit_subs;
    SingleConnectionMonitor *monit_pubs;
    SingleConnectionMonitor *monit_setup;

  protected:
    // A server instance will not have a socket for setting up the subscriber
    SubscriptionManagerInternals *internals;
    zmq::socket_t *setup_;
    Status _setup_status;
    SubStatus sub_status_;
};

#endif
