#ifndef __cw_CHANNEL_H__
#define __cw_CHANNEL_H__

#include <ostream>
#include <string>
#include <map>
#include <set>
#include "regular_expressions.h"
#include "value.h"
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
    Channel &operator=(const MachineRef &other);
};

std::ostream &operator<<(std::ostream &out, const MachineRef &m);

class ChannelImplementation {
public:
    ChannelImplementation() {}
    virtual ~ChannelImplementation();
    void addMonitor(const char *);
    void addMonitorPattern(const char *);
    void addMonitorProperty(const char *,Value &);
    void removeMonitor(const char *);
    void removeMonitorPattern(const char *);
    void removeMonitorProperty(const char *, Value &);
    
    void modified();
    void checked();
    
protected:
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
    ChannelDefinition(const char *name);
    Channel *instantiate(unsigned int port) const;
    static ChannelDefinition *find(const char *name);
    static ChannelDefinition *fromJSON(const char *json);
    char *toJSON();
    
    void setKey(const char *);
    void setIdent(const char *);
    void setVersion(const char *);
    void addShare(const char *);
    void addUpdates(const char *);
    void addSendName(const char *);
    void addReceiveName(const char *);
    void addOptionName(const char *n, Value &v);
    
    const std::string &getIdent() { return identifier; }
    const std::string &getKey() { return psk; }
    const std::string &getVersion() { return version; }

private:
    static std::map< std::string, ChannelDefinition* > *all;
    std::string psk; // preshared key
    std::string identifier;
    std::string version;
    std::set<std::string> shares;
    std::set<std::string> updates_names;
    std::set<std::string> send_messages;
    std::set<std::string> recv_messages;
    std::map<std::string, Value> options;
    
    friend class Channel;
};

class Channel : public ChannelImplementation {
public:
    typedef std::set< MachineRef* > MachineList;
    Channel(const std::string name);
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
    static Channel *create(unsigned int port, const ChannelDefinition *defn);
    static Channel *find(const std::string name);
    static void remove(const std::string name);
    static int uniquePort();
    static void sendPropertyChange(MachineInstance *machine, const Value &key, const Value &val);
    static void sendStateChange(MachineInstance *machine, std::string new_state);
    
    void setupFilters();
    
    bool matches(MachineInstance *machine, const std::string &name);
    bool patternMatches(const std::string &machine_name);
    bool filtersAllow(MachineInstance *machine);
    
    void setPort(unsigned int new_port);
    unsigned int getPort() const;
private:
    std::string name;
    unsigned int port;
    const ChannelDefinition *definition_;
    std::string identifier;
    std::string version;
    MachineList shares;
    MessagingInterface *mif;
    static std::map< std::string, Channel* > *all;
    std::set<MachineInstance*> machines;
};

std::ostream &operator<<(std::ostream &out, const Channel &m);


#endif
