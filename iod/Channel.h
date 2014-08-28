#ifndef __cw_CHANNEL_H__
#define __cw_CHANNEL_H__

#include <ostream>
#include <string>
#include <map>
#include <set>
#include "regular_expressions.h"
#include "value.h"
#include "symboltable.h"
#include "MachineInstance.h"
#include "MessagingInterface.h"
class Channel;
class MachineInterface;

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
    ChannelImplementation() : state(DISCONNECTED) {}
    virtual ~ChannelImplementation();
    void addMonitor(const char *);
    void addMonitorPattern(const char *);
    void addMonitorProperty(const char *,Value &);
    void removeMonitor(const char *);
    void removeMonitorPattern(const char *);
    void removeMonitorProperty(const char *, Value &);
    
    void modified();
    void checked();
    
    static State DISCONNECTED;
    static State CONNECTING;
    static State CONNECTED;
    
protected:
    State state;
    std::set<std::string> monitors_patterns;
    std::set<std::string> monitors_names;
    std::map<std::string, Value> monitors_properties;
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
    
    void setKey(const char *);
    void setIdent(const char *);
    void setVersion(const char *);
    void addShare(const char *);
    void addUpdates(const char *name, const char *interface_name);
    void addSendName(const char *);
    void addReceiveName(const char *);
    void addOptionName(const char *n, Value &v);
    
    const std::string &getIdent() { return identifier; }
    const std::string &getKey() { return psk; }
    const std::string &getVersion() { return version; }

private:
    static std::map< std::string, ChannelDefinition* > *all;
    std::map<std::string, MachineRef *> interfaces;
    ChannelDefinition *parent;
    std::string psk; // preshared key
    std::string identifier;
    std::string version;
    std::set<std::string> shares;
    std::map<std::string, Value> updates_names;
    std::set<std::string> send_messages;
    std::set<std::string> recv_messages;
    std::map<std::string, Value> options;
    
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


class Channel : public ChannelImplementation, public MachineInstance {
public:
    typedef std::set< MachineRef* > MachineList;
    Channel(const std::string name, const std::string type);
    ~Channel();
    Channel(const Channel &orig);
    Channel &operator=(const Channel &other);
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const Channel &other);

    bool doesMonitor(); // is this channel monitoring any machines?
    bool doesUpdate(); // does this channel update any machines?
    const ChannelDefinition *definition() const { return definition_; }
    void setDefinition(const ChannelDefinition *);
    
    const std::string &getName() const { return name; }
    
    static std::map< std::string, Channel* > *channels() { return all; }
    static Channel *create(unsigned int port, ChannelDefinition *defn);
    static Channel *find(const std::string name);
    static void remove(const std::string name);
    static int uniquePort();
    static void sendPropertyChange(MachineInstance *machine, const Value &key, const Value &val);
    static void sendStateChange(MachineInstance *machine, std::string new_state);
    static void setupAllShadows();
    static Channel *findByType(const std::string kind);
    MessagingInterface *getPublisher() { return mif; }
    
    void setupFilters();
    void setupShadows();
    void enableShadows();
    void disableShadows();
    void startPublisher();
    void startSubscriber();
    
    bool matches(MachineInstance *machine, const std::string &name);
    bool patternMatches(const std::string &machine_name);
    bool filtersAllow(MachineInstance *machine);
    
    void setPort(unsigned int new_port);
    unsigned int getPort() const;
    
    // poll channels and return number of descriptors with activity
    static int pollChannels(zmq::pollitem_t * &poll_items, long timeout, int n = 0);
    static void handleChannels();
    void setMessageHandler(MessageHandler *handler)  { message_handler = handler; }
    zmq::pollitem_t *getPollItems();
    
private:
    void checkCommunications();
    void setPollItemBase(zmq::pollitem_t *);

    std::string name;
    unsigned int port;
    const ChannelDefinition *definition_;
    std::string identifier;
    std::string version;
    MachineList shares;
    MessagingInterface *mif;
    static std::map< std::string, Channel* > *all;
    std::set<MachineInstance*> machines;
    SubscriptionManager *communications_manager;
    zmq::pollitem_t *poll_items;
    MessageHandler *message_handler;
};

std::ostream &operator<<(std::ostream &out, const Channel &m);

#endif
