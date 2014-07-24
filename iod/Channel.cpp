#include <iostream>
#include "Channel.h"
#include "MessagingInterface.h"
#include "MessageLog.h"
#include "MessageEncoding.h"

std::map<std::string, Channel*> *Channel::all = 0;
std::map< std::string, ChannelDefinition* > *ChannelDefinition::all = 0;

MachineRef::MachineRef() : refs(1) {
}

ChannelImplementation::~ChannelImplementation() { }


Channel::Channel(const std::string ch_name) : ChannelImplementation(), name(ch_name) {
    if (all == 0) {
        all = new std::map<std::string, Channel*>;
    }
    (*all)[name] = this;
}

Channel::~Channel() {
    std::set<MachineInstance*>::iterator iter = this->machines.begin();
    while (iter != machines.end()) {
        MachineInstance *machine = *iter++;
        machine->unpublish();
    }
	  this->machines.erase(machines.begin(), machines.end());
    remove(name);
    if (mif) delete mif;
}

Channel &Channel::operator=(const Channel &other) {
    assert(false);
    return *this;
}

std::ostream &Channel::operator<<(std::ostream &out) const  {
    out << "CHANNEL";
    return out;
}

std::ostream &operator<<(std::ostream &out, const Channel &m) {
    return m.operator<<(out);
}

bool Channel::operator==(const Channel &other) {
    return false;
}

int Channel::uniquePort() {
    int res = 0;
    char address_buf[40];
    while (true) {
        try{
            zmq::socket_t test_bind(*MessagingInterface::getContext(), ZMQ_PULL);
            res = random() % 200 + 7600;
            snprintf(address_buf, 40, "tcp://0.0.0.0:%d", res);
            test_bind.bind(address_buf);
            int linger = 0; // do not wait at socket close time
            test_bind.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
            break;
        }
        catch (zmq::error_t err) {
            if (zmq_errno() != EADDRINUSE) {
                break;
            }
        }
    }
    return res;
}

ChannelDefinition::ChannelDefinition(const char *n) : MachineClass(n) {
    if (!all) all = new std::map< std::string, ChannelDefinition* >;
    (*all)[n] = this;
}

ChannelDefinition *ChannelDefinition::find(const char *name) {
    if (!all) return 0;
    std::map< std::string, ChannelDefinition* >::iterator found = all->find(name);
    if (found == all->end()) return 0;
    return (*found).second;
}

Channel *ChannelDefinition::instantiate(int port) const {
    Channel *chn = Channel::create(port, this);
    return chn;
}

Channel *Channel::create(int port, const ChannelDefinition *defn) {
    char channel_name[100];
    snprintf(channel_name, 100, "%s:%d", defn->name.c_str(), port);
    Channel *chn = new Channel(channel_name);
    chn->setDefinition(defn);
    chn->setupFilters();
    chn->mif = MessagingInterface::create("*", port);
    return chn;
}

static void copyJSONArrayToSet(cJSON *obj, const char *key, std::set<std::string> &res) {
    cJSON *items = cJSON_GetObjectItem(obj, key);
    if (items && items->type == cJSON_Array) {
        cJSON *item = items->child;
        while (item) {
            if (item->type == cJSON_String) res.insert(item->valuestring);
            item = item->next;
        }
    }
}

static void copyJSONArrayToMap(cJSON *obj, const char *key, std::map<std::string, Value> &res) {
    cJSON *items = cJSON_GetObjectItem(obj, key);
    if (items && items->type == cJSON_Array) {
        cJSON *item = items->child;
        while (item) {
            // we only collect items from the array that match our expected format of
            // property,value pairs
            if (item->type == cJSON_Object) {
                cJSON *js_prop = cJSON_GetObjectItem(item, "property");
                cJSON *js_type = cJSON_GetObjectItem(item, "type");
                cJSON *js_val = cJSON_GetObjectItem(item, "value");
                if (js_prop->type == cJSON_String) {
                    res[js_prop->valuestring] = MessageEncoding::valueFromJSONObject(js_val, js_type);
                }
            }
            item = item->next;
        }
    }
}

ChannelDefinition *ChannelDefinition::fromJSON(const char *json) {
    cJSON *obj = cJSON_Parse(json);
    if (!obj) return 0;
/*    std::map<std::string, Value> options;
*/
    cJSON *item = cJSON_GetObjectItem(obj, "identifier");
    std::string name;
    if (item && item->type == cJSON_String) {
        name = item->valuestring;
    }
    ChannelDefinition *defn = new ChannelDefinition(name.c_str());
    defn->identifier = name;
    item = cJSON_GetObjectItem(obj, "key");
    if (item && item->type == cJSON_String)
        defn->setKey(item->valuestring);
    item = cJSON_GetObjectItem(obj, "version");
    if (item && item->type == cJSON_String)
        defn->setVersion(item->valuestring);
    copyJSONArrayToSet(obj, "monitors", defn->monitors_names);
    copyJSONArrayToSet(obj, "monitors_patterns", defn->monitors_patterns);
    copyJSONArrayToMap(obj, "monitors_properties", defn->monitors_properties);
    copyJSONArrayToSet(obj, "shares", defn->shares);
    copyJSONArrayToSet(obj, "updates", defn->updates_names);
    copyJSONArrayToSet(obj, "sends", defn->send_messages);
    copyJSONArrayToSet(obj, "receives", defn->recv_messages);
    return defn;
}

static cJSON *StringSetToJSONArray(std::set<std::string> &items) {
    cJSON *res = cJSON_CreateArray();
    std::set<std::string>::iterator iter = items.begin();
    while (iter != items.end()) {
        const std::string &str = *iter++;
        cJSON_AddItemToArray(res, cJSON_CreateString(str.c_str()));
    }
    return res;
}

static cJSON *MapToJSONArray(std::map<std::string, Value> &items) {
    cJSON *res = cJSON_CreateArray();
    std::map<std::string, Value>::iterator iter = items.begin();
    while (iter != items.end()) {
        const std::pair<std::string, Value> item = *iter++;
        cJSON *js_item = cJSON_CreateObject();
        cJSON_AddItemToObject(js_item, "property", cJSON_CreateString(item.first.c_str()));
        cJSON_AddStringToObject(js_item, "type", MessageEncoding::valueType(item.second).c_str());
        MessageEncoding::addValueToJSONObject(js_item, "value", item.second);
        cJSON_AddItemToArray(res, js_item);
    }
    return res;
}

char *ChannelDefinition::toJSON() {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "identifier", this->name.c_str());
    cJSON_AddStringToObject(obj, "key", this->psk.c_str());
    cJSON_AddStringToObject(obj, "version", this->version.c_str());
    cJSON_AddItemToObject(obj, "monitors", StringSetToJSONArray(this->monitors_names));
    cJSON_AddItemToObject(obj, "monitors_patterns", StringSetToJSONArray(this->monitors_patterns));
    cJSON_AddItemToObject(obj, "monitors_properties", MapToJSONArray(this->monitors_properties));
    cJSON_AddItemToObject(obj, "shares", StringSetToJSONArray(this->shares));
    cJSON_AddItemToObject(obj, "updates", StringSetToJSONArray(this->updates_names));
    cJSON_AddItemToObject(obj, "sends", StringSetToJSONArray(this->send_messages));
    cJSON_AddItemToObject(obj, "receives", StringSetToJSONArray(this->recv_messages));
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return str;
}

Channel *Channel::find(const std::string name) {
    if (!all) return 0;
    std::map<std::string, Channel *>::iterator found = all->find(name);
    if (found == all->end()) return 0;
    return (*found).second;
}

void Channel::remove(const std::string name) {
    if (!all) return;
    std::map<std::string, Channel *>::iterator found = all->find(name);
    if (found == all->end()) return;
    all->erase(found);
}

void Channel::setDefinition(const ChannelDefinition *def) {
    definition_ = def;
}

void Channel::sendPropertyChange(MachineInstance *machine, const Value &key, const Value &val) {
    if (!all) return;
    std::string name = machine->fullName();
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
        bool matches = false;
        if (chn->definition()->monitors_names.count(name))
            matches = true;
        else if (chn->machines.count(machine))
            matches = true;
        else
            continue; // only test the channel instance if the channel definition matches
        
        if (!chn->monitors_names.empty() && !chn->monitors_names.count(name))
            matches = false;
        
        if (!matches && !chn->monitors_patterns.empty()) {
            // no match on name but we still may match on pattern
            std::set<std::string>::iterator iter = chn->monitors_patterns.begin();
            while (iter != chn->monitors_patterns.end()) {
                const std::string &pattern = *iter++;
                rexp_info *rexp = create_pattern(pattern.c_str());
                if (!rexp->compilation_error) {
                    if (execute_pattern(rexp, machine->getName().c_str()) == 0) {
                        matches = true;
                    }
                }
                else {
                    MessageLog::instance()->add(rexp->compilation_error);
                    std::cerr << "Channel error: " << chn->name << " " << rexp->compilation_error << "\n";
                    return;
                }
            }
        }
        
        if (matches)
            chn->mif->sendCommand("PROPERTY", name, key, val); // send command
    }
}

void Channel::sendStateChange(MachineInstance *machine, std::string new_state) {
    if (!all) return;
    std::string name = machine->fullName();
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
        bool matches = false;
        if (chn->definition()->monitors_names.count(name))
            matches = true;
        else if (chn->machines.count(machine))
            matches = true;
        else
            continue; // only test the channel instance if the channel definition matches
        
        size_t n = chn->monitors_names.count(name);
        if (!chn->monitors_names.empty() && !n)
            matches = false;
        
        if (!matches && !chn->monitors_patterns.empty()) {
            // no match on name but we still may match on pattern
            std::set<std::string>::iterator iter = chn->monitors_patterns.begin();
            while (iter != chn->monitors_patterns.end()) {
                const std::string &pattern = *iter++;
                rexp_info *rexp = create_pattern(pattern.c_str());
                if (!rexp->compilation_error) {
                    if (execute_pattern(rexp, machine->getName().c_str()) == 0) {
                        matches = true;
                    }
                }
                else {
                    MessageLog::instance()->add(rexp->compilation_error);
                    std::cerr << "Channel error: " << chn->name << " " << rexp->compilation_error << "\n";
                    return;
                }
            }
        }
        
        if (matches)
            chn->mif->sendState("STATE", name, new_state); // send state
    
    }
}

void ChannelDefinition::setKey(const char *s) {
    psk = s;
}

void ChannelDefinition::setIdent(const char *s) {
    identifier = s;
}
void ChannelDefinition::setVersion(const char *s) {
    version = s;
}
void ChannelDefinition::addShare(const char *s) {
    shares.insert(s);
}
void ChannelImplementation::addMonitor(const char *s) {
    std::cerr << "add monitor for " << s << "\n";
    monitors_names.insert(s);
}
void ChannelImplementation::removeMonitor(const char *s) {
    std::cerr << "remove monitor for " << s << "\n";
    monitors_names.erase(s);
}
void ChannelImplementation::addMonitorPattern(const char *s) {
    monitors_patterns.insert(s);
}
void ChannelImplementation::addMonitorProperty(const char *key,Value &val) {
    monitors_properties[key] = val;
}
void ChannelImplementation::removeMonitorProperty(const char *key,Value &val) {
    monitors_properties.erase(key);
    //TBD Bug here, we should be using a set< pair<string, Value> >, not a map
}

void ChannelImplementation::removeMonitorPattern(const char *s) {
    monitors_patterns.erase(s);
}
void ChannelDefinition::addUpdates(const char *s) {
    updates_names.insert(s);
}
void ChannelDefinition::addSendName(const char *s) {
    send_messages.insert(s);
}
void ChannelDefinition::addReceiveName(const char *s) {
    recv_messages.insert(s);
}
void ChannelDefinition::addOptionName(const char *n, Value &v) {
    options[n] = v;
}

void Channel::setupFilters() {
    std::set<std::string>::iterator iter = definition()->monitors_patterns.begin();
    while (iter != definition()->monitors_patterns.end()) {
        const std::string &pattern = *iter++;
        rexp_info *rexp = create_pattern(pattern.c_str());
        if (!rexp->compilation_error) {
            std::list<MachineInstance*>::iterator machines = MachineInstance::begin();
            while (machines != MachineInstance::end()) {
                MachineInstance *machine = *machines++;
                if (execute_pattern(rexp, machine->getName().c_str()) == 0) {
                    machine->publish();
                    this->machines.insert(machine);
                }
            }
        }
        else {
            MessageLog::instance()->add(rexp->compilation_error);
            std::cerr << "Channel error: " << definition()->name << " " << rexp->compilation_error << "\n";
        }
    }
    iter = definition()->monitors_names.begin();
    while (iter != definition()->monitors_names.end()) {
        const std::string &name = *iter++;
        MachineInstance *machine = MachineInstance::find(name.c_str());
        if (machine && !this->machines.count(machine))
            machine->publish();
    }
}

