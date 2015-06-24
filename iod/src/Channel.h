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

#ifndef __cw_CHANNEL_H__
#define __cw_CHANNEL_H__

#include <ostream>
#include <string>
#include <map>
#include <set>
#include <boost/thread.hpp>
#include "regular_expressions.h"
#include "value.h"
#include "symboltable.h"
#include "MachineInstance.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "IODCommand.h"

class Channel;
class MachineInterface;
class SubscriptionManager;

struct MachineRef {
public:
    static MachineRef *create(const std::string name, MachineInstance *machine, MachineInterface *iface);
    MachineRef *ref() { ++refs; return this; }
    void release() { --refs; if (!refs)  delete this; }
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const MachineRef &other);

private:
    std::string key;
    std::string name;
    std::string interface_name;
    MachineInstance *item;
    MachineInterface *interface;
    int refs;
    MachineRef();
    MachineRef(const MachineRef &orig);
    MachineRef &operator=(const MachineRef &other);
};

std::ostream &operator<<(std::ostream &out, const MachineRef &m);

class ChannelImplementation {
public:
    ChannelImplementation() : monitors_exports(false) {}
    virtual ~ChannelImplementation();
    void addMonitor(const char *);
    void addIgnorePattern(const char *);
    void addMonitorPattern(const char *);
    void addMonitorProperty(const char *,Value &);
    void addMonitorExports();
    void removeMonitor(const char *);
    void removeIgnorePattern(const char *);
    void removeMonitorPattern(const char *);
    void removeMonitorProperty(const char *, Value &);
    void removeMonitorExports();
    
    void modified();
    void checked();

	bool monitors() const; // does this channel monitor anything?
	bool updates() const; // does this channel update anything?
	bool shares_machines() const; // does this channel share anything?

    static State DISCONNECTED;
	static State CONNECTING;
	static State CONNECTED;
    static State DOWNLOADING;
	static State UPLOADING;
	static State ACTIVE;

protected:
    std::set<std::string> monitors_patterns;
    std::set<std::string> ignores_patterns;
    std::set<std::string> monitors_names;
    std::map<std::string, Value> monitors_properties;
	std::map<std::string, Value> updates_names;
	std::map<std::string, Value> shares_names;
    bool monitors_exports;
    uint64_t last_modified; // if the modified time > check time, a full check will be used
    uint64_t last_checked;

private:
    ChannelImplementation(const ChannelImplementation &);
    ChannelImplementation & operator=(const ChannelImplementation &);
};

class ChannelDefinition : public ChannelImplementation, public MachineClass {
public:
    ChannelDefinition(const char *name, ChannelDefinition *parent = 0);
    Channel *instantiate(unsigned int port);
    static void instantiateInterfaces();
    static ChannelDefinition *find(const char *name);
    static ChannelDefinition *fromJSON(const char *json);
    char *toJSON();
	bool isPublisher() const; // channel can be shared by multiple subscribers
	void setPublisher(bool which);
    
    void setKey(const char *);
    void setIdent(const char *);
    void setVersion(const char *);
	void addShare(const char *nm, const char *if_nm);
	void addUpdates(const char *name, const char *interface_name);
	void addShares(const char *name, const char *interface_name);
    void addSendName(const char *);
    void addReceiveName(const char *);
    void addOptionName(const char *n, Value &v);

	void setThrottleTime(unsigned int t) { throttle_time = t; }
	unsigned int getThrottleTime() { return throttle_time; }
    
    const std::string &getIdent() { return identifier; }
    const std::string &getKey() { return psk; }
    const std::string &getVersion() { return version; }

		Value getValue(const char *property);

private:
    static std::map< std::string, ChannelDefinition* > *all;
    std::map<std::string, MachineRef *> interfaces;
    ChannelDefinition *parent;
	bool ignore_response;
    std::string psk; // preshared key
    std::string identifier;
    std::string version;
    std::set<std::string> send_messages;
    std::set<std::string> recv_messages;
    std::map<std::string, Value> options;
	unsigned int throttle_time;
	
    friend class Channel;
};


class MessageHandler {
public:
    MessageHandler();
    virtual ~MessageHandler();
    virtual void handleMessage(const char *buf, size_t len);
    virtual bool receiveMessage(zmq::socket_t &sock) = 0;
    char *data;
    size_t data_size;
};

class MachineRecord {
public:
	MachineInstance *machine;
	uint64_t last_sent;
	std::map<std::string, Value> properties;
};

class ChannelMessage {
public:
	int message_id;
	std::string text;
	std::string response_;
	bool done;
	ChannelMessage(int msg_id, const char *msg) : message_id(msg_id), text(msg), done(false) { }
	std::string & response() { return response_; }
	void setResponse(const char *res) {  response_ = res; done = true; }
};

class IODCommand;
class Channel : public ChannelImplementation, public MachineInstance {
public:
    typedef std::set< MachineRef* > MachineList;
    Channel(const std::string name, const std::string type);
    ~Channel();
    Channel(const Channel &orig);
    Channel &operator=(const Channel &other);
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const Channel &other);

	void operator()();
	void abort();

	virtual Action::Status setState(State &new_state, bool resume = false);
	virtual Action::Status setState(const char *new_state, bool resume = false);

    bool doesMonitor(); // is this channel monitoring any machines?
    bool doesUpdate(); // does this channel update any machines?
	bool doesShare(); // does this channel share any machines?
    const ChannelDefinition *definition() const { return definition_; }
    void setDefinition(const ChannelDefinition *);
    
    const std::string &getName() const { return name; }
    
    static std::map< std::string, Channel* > *channels() { return all; }
    static Channel *create(unsigned int port, ChannelDefinition *defn);
    static Channel *find(const std::string name);
    static void remove(const std::string name);
    static int uniquePort(unsigned int range_start = 7600, unsigned int range_end = 7799);
    static void sendPropertyChange(MachineInstance *machine, const Value &key, const Value &val);
    static void sendStateChange(MachineInstance *machine, std::string new_state);
	static void sendCommand(MachineInstance *machine, std::string cmd, std::list<Value>*params);
    static void setupAllShadows();
    static Channel *findByType(const std::string kind);
    MessagingInterface *getPublisher() { return mif; }
    
    void setupFilters();
    void setupShadows();
    void enableShadows();
    void disableShadows();
	void startServer(Protocol proto = eZMQ);// used by shared (publish/subscribe) and one-to-one channels
	void startClient();	// used by shared (publish/subscribe) channels
    void startSubscriber();
	void stopSubscriber();
	void stopServer();
	bool isClient(); 	// does this channel connect to another instance of clockwork?
	void syncRemoteStates();
	void syncInterfaceProperties(MachineInstance *m);
	zmq::socket_t *createCommandSocket(bool client_endpoint);

	void start(); // begin executing by making connections or serving
	void stop();
	bool started();
	void enable(); // enable messages to pass through the channel
	void disable();
	void checkStateChange(); // change state after receiving a done from current state
    
    bool matches(MachineInstance *machine, const std::string &name);
    bool patternMatches(const std::string &machine_name);
    bool filtersAllow(MachineInstance *machine);
    
	void setPort(unsigned int new_port);
	unsigned int getPort() const;

	void setThrottleTime(unsigned int t) { throttle_time = t; }

    // poll channels and return number of descriptors with activity
	static void startChannels();
	static void stopChannels();
    static int pollChannels(zmq::pollitem_t * &poll_items, long timeout, int n = 0);
    static void handleChannels();
    void setMessageHandler(MessageHandler *handler)  { message_handler = handler; }
    zmq::pollitem_t *getPollItems();
	bool sendMessage(const char *msg, zmq::socket_t &sock, std::string &response);

	static void sendPropertyChanges(MachineInstance *machine);
	typedef std::map<std::string, Value> PropertyRecords;

	void addConnection();
	void dropConnection();

private:
	static std::map<MachineInstance *, MachineRecord> pending_items;
	std::list<IODCommand *>pending_commands;
	std::list<IODCommand *>completed_commands;

	void checkCommunications();
    void setPollItemBase(zmq::pollitem_t *);

    std::string name;
    unsigned int port;
    const ChannelDefinition *definition_;
    std::string identifier;
    std::string version;
    MachineList shared_machines;
    MessagingInterface *mif;
    static std::map< std::string, Channel* > *all;
    std::set<MachineInstance*> channel_machines;
    SubscriptionManager *communications_manager;
    zmq::pollitem_t *poll_items;
    MessageHandler *message_handler;
	SocketMonitor *monit_subs;
	SocketMonitor *monit_pubs;
	EventResponder *connect_responder;
	EventResponder *disconnect_responder;
	boost::thread *monitor_thread;

	unsigned int throttle_time;
	boost::mutex update_mutex;

	int connections;
	bool aborted;
	boost::thread *subscriber_thread;
	bool started_;
	zmq::socket_t *cmd_client;
	zmq::socket_t *cmd_server;
	SequenceNumber seqno;
	std::list<ChannelMessage> pending_messages;
	std::list<ChannelMessage> completed_messages;
};

std::ostream &operator<<(std::ostream &out, const Channel &m);

#endif
