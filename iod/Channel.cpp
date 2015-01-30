#include <iostream>
#include <map>
#include <set>
#include <stdlib.h>
#include <string.h>
#include "Channel.h"
#include "MessagingInterface.h"
#include "MessageLog.h"
#include "MessageEncoding.h"
#include "value.h"

std::map<std::string, Channel*> *Channel::all = 0;
std::map< std::string, ChannelDefinition* > *ChannelDefinition::all = 0;

State ChannelImplementation::DISCONNECTED("DISCONNECTED");
State ChannelImplementation::CONNECTING("CONNECTING");
State ChannelImplementation::CONNECTED("CONNECTED");


MachineRef::MachineRef() : refs(1) {
}

ChannelImplementation::~ChannelImplementation() { }


Channel::Channel(const std::string ch_name, const std::string type)
        : ChannelImplementation(), MachineInstance(ch_name.c_str(), type.c_str()),
            name(ch_name), port(0), mif(0), communications_manager(0)
{
    if (all == 0) {
        all = new std::map<std::string, Channel*>;
    }
    (*all)[name] = this;
}

Channel::~Channel() {
    disableShadows();
    std::set<MachineInstance*>::iterator iter = this->machines.begin();
    while (iter != machines.end()) {
        MachineInstance *machine = *iter++;
        machine->unpublish();
    }
    this->machines.erase(machines.begin(), machines.end());
    remove(name);
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


void Channel::setPort(unsigned int new_port) {
    assert(port == 0);
    port = new_port;
}
unsigned int Channel::getPort() const { return port; }


int Channel::uniquePort(unsigned int start, unsigned int end) {
    int res = 0;
    char address_buf[40];
    while (true) {
        try{
            zmq::socket_t test_bind(*MessagingInterface::getContext(), ZMQ_PULL);
            res = random() % (end-start+1) + start;
            snprintf(address_buf, 40, "tcp://0.0.0.0:%d", res);
            test_bind.bind(address_buf);
            int linger = 0; // do not wait at socket close time
            test_bind.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
            std::cout << "found available port " << res << "\n";
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

ChannelDefinition::ChannelDefinition(const char *n, ChannelDefinition *prnt) : MachineClass(n), parent(prnt) {
    if (!all) all = new std::map< std::string, ChannelDefinition* >;
    (*all)[n] = this;
}

ChannelDefinition *ChannelDefinition::find(const char *name) {
    if (!all) return 0;
    std::map< std::string, ChannelDefinition* >::iterator found = all->find(name);
    if (found == all->end()) return 0;
    return (*found).second;
}

Channel *ChannelDefinition::instantiate(unsigned int port) {
    
    Channel *chn = Channel::findByType(name);
    if (!chn) chn = Channel::create(port, this);
    return chn;
}

void Channel::startPublisher() {
    assert(!mif);
    mif = MessagingInterface::create("*", port);
}

void Channel::startSubscriber() {
    assert(!communications_manager);
    
    Value host = getValue("host");
    Value port_val = getValue("port");
    if (host == SymbolTable::Null)
        host = "localhost";
    long port = 0;
    if (!port_val.asInteger(port)) port = 5555;
    //char buf[150];
    ////snprintf(buf, 150, "tcp://%s:%d", host.asString().c_str(), (int)port);
    std::cout << " channel " << _name << " " << port << "\n";
    communications_manager = new SubscriptionManager(definition()->name.c_str(),
                              host.asString().c_str(), (int)port);

}

void ChannelDefinition::instantiateInterfaces() {
    // find all machines listed in the updates clauses of channel definitions
    if (!all) return; // no channel definitions to look through
    std::map< std::string, ChannelDefinition* >::iterator iter = all->begin();
    while (iter != all->end()) {
        const std::pair<std::string, ChannelDefinition*> item = *iter++;
        std::map<std::string, Value>::iterator updated_machines = item.second->updates_names.begin();
        while (updated_machines != item.second->updates_names.end()) {
            const std::pair<std::string, Value> &instance_name = *updated_machines++;
            MachineInstance *found = MachineInstance::find(instance_name.first.c_str());
            if (!found) { // instantiate a shadow to represent this machine
                if (item.second) {
                    MachineInstance *m = MachineInstanceFactory::create(instance_name.first.c_str(),
                                                                        instance_name.second.asString().c_str(),
                                                                        MachineInstance::MACHINE_SHADOW);
                    m->setDefinitionLocation("dynamic", 0);
                    m->setProperties(item.second->properties);
                    m->setStateMachine(item.second);
                    m->setValue("startup_enabled", false);
                    machines[instance_name.first] = m;
                }
                else {
                    char buf[150];
                    snprintf(buf, 150, "Error: no interface named %s", item.first.c_str());
                    MessageLog::instance()->add(buf);
                }
            }
        }
    }
    // channels automatically setup monitoring on machines they update
    // when the channel is instantiated. At startup, however, some machines
    // may not have been defined when the channel is created so we need
    // to do an extra pass here.
    Channel::setupAllShadows();
}

Channel *Channel::create(unsigned int port, ChannelDefinition *defn) {
    assert(defn);
    char channel_name[100];
    snprintf(channel_name, 100, "%s:%d", defn->name.c_str(), port);
    Channel *chn = new Channel(channel_name, defn->name);

    chn->setPort(port);
    chn->setDefinition(defn);
    chn->setStateMachine(defn);
    if (defn->monitors_exports) chn->monitors_exports = true;
    chn->modified();
    chn->setupShadows();
    chn->setupFilters();
    //chn->mif = MessagingInterface::create("*", port);
    chn->enableShadows();
    return chn;
}

void ChannelImplementation::modified() {
    struct timeval now;
    gettimeofday(&now, 0);
    last_modified = now.tv_sec * 1000000L + now.tv_usec;
}

void ChannelImplementation::checked() {
    struct timeval now;
    gettimeofday(&now, 0);
    last_checked = now.tv_sec * 1000000L + now.tv_usec;
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

static void copyJSONArrayToMap(cJSON *obj, const char *key, std::map<std::string, Value> &res, const char *key_name = "property", const char *value_name = "type") {
    cJSON *items = cJSON_GetObjectItem(obj, key);
    if (items && items->type == cJSON_Array) {
        cJSON *item = items->child;
        while (item) {
            // we only collect items from the array that match our expected format of
            // property,value pairs
            if (item->type == cJSON_Object) {
                cJSON *js_prop = cJSON_GetObjectItem(item, key_name);
                cJSON *js_type = cJSON_GetObjectItem(item, value_name);
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
    item = cJSON_GetObjectItem(obj, "monitors_exports");
    if (item && item->type == cJSON_True)
        defn->monitors_exports = true;
    else if (item && item->type == cJSON_False)
        defn->monitors_exports = false;

    copyJSONArrayToSet(obj, "shares", defn->shares);
    copyJSONArrayToMap(obj, "updates", defn->updates_names);
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

static cJSON *MapToJSONArray(std::map<std::string, Value> &items, const char *key_name = "property", const char *value_name = "type") {
    cJSON *res = cJSON_CreateArray();
    std::map<std::string, Value>::iterator iter = items.begin();
    while (iter != items.end()) {
        const std::pair<std::string, Value> item = *iter++;
        cJSON *js_item = cJSON_CreateObject();
        cJSON_AddItemToObject(js_item, key_name, cJSON_CreateString(item.first.c_str()));
        cJSON_AddStringToObject(js_item, value_name, MessageEncoding::valueType(item.second).c_str());
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
    if (this->monitors_exports)
        cJSON_AddTrueToObject(obj, "monitors_exports");
    else
        cJSON_AddFalseToObject(obj, "monitors_exports");
    cJSON_AddItemToObject(obj, "shares", StringSetToJSONArray(this->shares));
    cJSON_AddItemToObject(obj, "updates", MapToJSONArray(this->updates_names, "name","type"));
    cJSON_AddItemToObject(obj, "sends", StringSetToJSONArray(this->send_messages));
    cJSON_AddItemToObject(obj, "receives", StringSetToJSONArray(this->recv_messages));
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return str;
}



MessageHandler::MessageHandler() : data(0), data_size(0) {
}

MessageHandler::~MessageHandler() {
    if (data) free(data);
}

void MessageHandler::handleMessage(const char *buf, size_t len) {
    if (data) { free(data); data = 0; data_size = 0; }
    data = (char*)malloc(len);
    memcpy(data, buf, len);
}

bool MessageHandler::receiveMessage(zmq::socket_t &sock) {
    if (data) { free(data); data = 0; data_size = 0; }
    zmq::message_t update;
    if (sock.recv(&update, ZMQ_NOBLOCK)) {
        long len = update.size();
        char *data = (char *)malloc(len+1);
        memcpy(data, update.data(), len);
        data[len] = 0;
        return true;
    }
    return false;
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
        
        if (!chn->machines.count(machine))
            continue;
        if (chn->filtersAllow(machine)) {
            //if (!chn->machines.count(machine)) chn->machines.insert(machine);
            if (chn->communications_manager) {
                std::string response;
                char *cmd = MessageEncoding::encodeCommand("PROPERTY", name, key, val); // send command
                sendMessage(cmd, chn->communications_manager->setup,response);
                std::cout << "channel " << name << " got response: " << response << "\n";
								free(cmd);
            }
            else if (chn->mif) {
                char *cmd = MessageEncoding::encodeCommand("PROPERTY", name, key, val); // send command
                chn->mif->send(cmd);
								free(cmd);
            }
            else {
                std::cout << "channel " << name << " wants to send property change\n";
            }
        }
    }
}

bool Channel::matches(MachineInstance *machine, const std::string &name) {
    if (definition()->monitors_names.count(name))
        return true;
    else if (machines.count(machine))
        return true;
    return false; // only test the channel instance if the channel definition matches
}

bool Channel::patternMatches(const std::string &machine_name) {
    // no match on name but we still may match on pattern
    std::set<std::string>::iterator iter = monitors_patterns.begin();
    while (iter != monitors_patterns.end()) {
        const std::string &pattern = *iter++;
        rexp_info *rexp = create_pattern(pattern.c_str());
        if (!rexp->compilation_error) {
            if (execute_pattern(rexp, machine_name.c_str()) == 0) {
                return true;
            }
        }
        else {
            MessageLog::instance()->add(rexp->compilation_error);
            std::cerr << "Channel error: " << name << " " << rexp->compilation_error << "\n";
            return false;
        }
    }
    return false;
}

bool Channel::doesUpdate() {
    return definition()->updates_names.empty();
}

bool Channel::doesMonitor() {
    return  monitors_exports || definition()->monitors_exports
        || ! (definition()->monitors_names.empty() && definition()->monitors_patterns.empty() && definition()->monitors_properties.empty()
              && monitors_names.empty() && monitors_patterns.empty() && monitors_properties.empty());
}

bool Channel::filtersAllow(MachineInstance *machine) {
    if ((definition()->monitors_exports || monitors_exports) && !machine->modbus_exports.empty())
        return true;
    
    if (!matches(machine, name)) return false;
    
    if (monitors_names.empty() && monitors_patterns.empty() && monitors_properties.empty())
        return true;
    
    // apply channel specific filters
    size_t n = monitors_names.count(name);
    if (!monitors_names.empty() && n)
        return true;
    
    // if patterns are given and this machine doesn't match anything else, we try patterns
    if (!monitors_patterns.empty()) {
        if (patternMatches(machine->getName()))
            return true;
    }
    
    // if properties are given and this machine doesn't match anything else, we try properties
    if (!monitors_properties.empty()) {
        // TBD check properties
    }
    return false;
}

Channel *Channel::findByType(const std::string kind) {
    if (!all) return 0;
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
        if (chn->_type == kind) return chn;
    }
    return 0;
}


void Channel::sendStateChange(MachineInstance *machine, std::string new_state) {
    if (!all) return;
    std::string name = machine->fullName();
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;

        if (!chn->machines.count(machine))
            continue;
        if (chn->filtersAllow(machine)) {
            //if (!chn->machines.count(machine)) chn->machines.insert(machine);
            if (chn->communications_manager
                && chn->communications_manager->setupStatus() == SubscriptionManager::e_done ) {
                std::string response;
                char *cmd = MessageEncoding::encodeState(name, new_state); // send command
                sendMessage(cmd, chn->communications_manager->setup,response);
                std::cout << "channel " << name << " got response: " << response << "\n";
            }
            else if (chn->mif) {
                char *cmd = MessageEncoding::encodeState(name, new_state); // send command
                chn->mif->send(cmd);
            }
            else {
                char buf[150];
                snprintf(buf, 150, "Warning: machine %s changed state but the channel is not connected",
                         machine->getName().c_str());
                MessageLog::instance()->add(buf);
            }
        }
    }
}

void Channel::sendCommand(MachineInstance *machine, std::string command, std::list<Value>*params) {
    if (!all) return;
    std::string name = machine->fullName();
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;

        if (!chn->machines.count(machine))
            continue;
        if (chn->filtersAllow(machine)) {
            //if (!chn->machines.count(machine)) chn->machines.insert(machine);
            if (chn->communications_manager
                && chn->communications_manager->setupStatus() == SubscriptionManager::e_done ) {
                std::string response;
                char *cmd = MessageEncoding::encodeCommand(command, params); // send command
                sendMessage(cmd, chn->communications_manager->setup,response);
                std::cout << "channel " << name << " got response: " << response << "\n";
								free(cmd);
            }
            else if (chn->mif) {
                char *cmd = MessageEncoding::encodeCommand(command, params); // send command
                chn->mif->send(cmd);
								free(cmd);
            }
            else {
                char buf[150];
                snprintf(buf, 150, "Warning: machine %s changed state but the channel is not connected",
                machine->getName().c_str());
                MessageLog::instance()->add(buf);
            }
        }
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
    modified();
}
void ChannelImplementation::addIgnorePattern(const char *s) {
    std::cerr << "add " << s << " to ignore list\n";
    ignores_patterns.insert(s);
    modified();
}
void ChannelImplementation::removeIgnorePattern(const char *s) {
    std::cerr << "remove " << s << " from ignore list\n";
    ignores_patterns.erase(s);
    modified();
}
void ChannelImplementation::removeMonitor(const char *s) {
    std::cerr << "remove monitor for " << s;
	if (monitors_names.count(s)) {
	    monitors_names.erase(s);
    	modified();
		std::cerr << "\n";
	}
	else {
		std::cerr << "...not found\n";
		std::string pattern = "^";
		pattern += s;
		pattern += "$";
		addIgnorePattern(pattern.c_str());
	}
}
void ChannelImplementation::addMonitorPattern(const char *s) {
    monitors_patterns.insert(s);
    modified();
}
void ChannelImplementation::addMonitorProperty(const char *key,Value &val) {
    monitors_properties[key] = val;
    modified();
}
void ChannelImplementation::removeMonitorProperty(const char *key,Value &val) {
    monitors_properties.erase(key);
    modified();
    //TBD Bug here, we should be using a set< pair<string, Value> >, not a map
}
void ChannelImplementation::addMonitorExports() {
    monitors_exports = true;
    modified();
}
void ChannelImplementation::removeMonitorExports() {
    monitors_exports = false;
    modified();
}

void ChannelImplementation::removeMonitorPattern(const char *s) {
    monitors_patterns.erase(s);
    modified();
}
void ChannelDefinition::addUpdates(const char *nm, const char *if_nm) {
    updates_names[nm] = if_nm;
    modified();
}
void ChannelDefinition::addSendName(const char *s) {
    send_messages.insert(s);
    modified();
}
void ChannelDefinition::addReceiveName(const char *s) {
    recv_messages.insert(s);
    modified();
}
void ChannelDefinition::addOptionName(const char *n, Value &v) {
    options[n] = v;
    modified();
}

/* When a channel is created and connected, instances of updated machines on that channel are enabled.
 */
void Channel::enableShadows() {
    // enable all machines that are updated by this channel
    std::map<std::string, Value>::const_iterator iter = definition()->updates_names.begin();
    while (iter != definition()->updates_names.end()) {
        const std::pair< std::string, Value> item = *iter++;
        MachineInstance *m = MachineInstance::find(item.first.c_str());
        MachineShadowInstance *ms = dynamic_cast<MachineShadowInstance*>(m);
        if (ms) ms->enable();
    }
}

void Channel::disableShadows() {
    // disable all machines that are updated by this channel
    std::map<std::string, Value>::const_iterator iter = definition()->updates_names.begin();
    while (iter != definition()->updates_names.end()) {
        const std::pair< std::string, Value> item = *iter++;
        MachineInstance *m = MachineInstance::find(item.first.c_str());
        MachineShadowInstance *ms = dynamic_cast<MachineShadowInstance*>(m);
        if (ms) ms->disable();
    }
}

void Channel::setupShadows() {
    // decide if the machines updated on this channel are instantiated as
    // normal machines and if so, publish their changes to this channel.
    if (!definition_) definition_ = ChannelDefinition::find(this->_type.c_str());
    if (!definition_) {
        char buf[150];
        snprintf(buf, 150, "Error: channel definition %s for %s could not be found", name.c_str(), _type.c_str());
        MessageLog::instance()->add(buf);
        return;
    }
    
    std::map<std::string, Value>::const_iterator iter = definition()->updates_names.begin();
    while (iter != definition()->updates_names.end()) {
        const std::pair< std::string, Value> item = *iter++;
        MachineInstance *m = MachineInstance::find(item.first.c_str());
        MachineShadowInstance *ms = dynamic_cast<MachineShadowInstance*>(m);
        if (m && !ms) { // this machine is not a shadow.
            if (!machines.count(m)) {
                m->publish();
                machines.insert(m);
            }
        }
        else if (m) {
            // this machine is a shadow
        }
    }
    
    
    //- TBD should this only subscribe if channel monitors a machine?
}

// poll channels and return number of descriptors with activity
// if n is 0 the block of data will be reallocated,
// otherwise up to n items will be initialised
int Channel::pollChannels(zmq::pollitem_t * &poll_items, long timeout, int n) {
    if (!all) return 0;
    if (!n) {
        std::map<std::string, Channel*>::iterator iter = all->begin();
        while (iter != all->end()) {
            const std::pair<std::string, Channel *> &item = *iter++;
            Channel *chn = item.second;
            if (chn->communications_manager) n += chn->communications_manager->numSocks();
        }
        if (poll_items) delete poll_items;
        poll_items = new zmq::pollitem_t[n];
    }
    int count = 0;
    zmq::pollitem_t *curr = poll_items;
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        const std::pair<std::string, Channel *> &item = *iter++;
        Channel *chn = item.second;
        if (chn->communications_manager) {
            chn->communications_manager->checkConnections();
            if (chn->communications_manager && n >= count + chn->communications_manager->numSocks()) {
                count += chn->communications_manager->configurePoll(curr);
                chn->setPollItemBase(curr);
                curr += count;
            }
        }
    }
    int rc = 0;
    while (true) {
        try {
            
            int rc = zmq::poll(poll_items, count, timeout);
            if (rc == -1 && errno == EAGAIN) continue;
            break;
        }
        catch(zmq::error_t e) {
            if (errno == EINTR) continue;
            char buf[150];
            snprintf(buf, 150, "Channel error: %s", zmq_strerror(errno));
            MessageLog::instance()->add(buf);
            std::cerr << buf << "\n";
            break;
        }
    }
    if (rc>0) std::cout << rc << " channels with activity\n";
    return rc;
}

void Channel::handleChannels() {
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        const std::pair<std::string, Channel *> &item = *iter++;
        Channel *chn = item.second;
        chn->checkCommunications();
    }
}


void Channel::setPollItemBase(zmq::pollitem_t *base) {
    poll_items = base;
}

void Channel::checkCommunications() {
    Value *state = current_state.getNameValue();
    bool ok = communications_manager->checkConnections();
    if (!ok) {
        if (communications_manager->monit_setup->disconnected() && state->token_id != ChannelImplementation::DISCONNECTED.getId())
            setState(ChannelImplementation::DISCONNECTED);
        else if (!communications_manager->monit_setup->disconnected()) {
            if (communications_manager->setupStatus() == SubscriptionManager::e_waiting_connect && state->token_id != ChannelImplementation::CONNECTING.getId()) {
                setState(ChannelImplementation::CONNECTING);
            }
        }
        return;
    }
    if ( state->token_id != ChannelImplementation::CONNECTED.getId())
        setState(ChannelImplementation::CONNECTED);
    
    if ( !(poll_items[1].revents & ZMQ_POLLIN) && message_handler) {
        if (message_handler->receiveMessage(communications_manager->subscriber))
            std::cout << "Channel got message: " << message_handler->data << "\n";
    }

}

void Channel::setupAllShadows() {
    if (!all) return;
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
        if (chn) chn->setupShadows();
    }
}

void Channel::setupFilters() {
    checked();
    // check if this channl monitors exports and if so, add machines that have exports
    if (definition()->monitors_exports || monitors_exports) {
        std::list<MachineInstance*>::iterator machines = MachineInstance::begin();
        while (machines != MachineInstance::end()) {
            MachineInstance *machine = *machines++;
            if (! machine->modbus_exports.empty() ) {
                this->machines.insert(machine);
                machine->publish();
            }
        }
    }
        
    std::set<std::string>::iterator iter = definition()->monitors_patterns.begin();
    while (iter != definition()->monitors_patterns.end()) {
        const std::string &pattern = *iter++;
        rexp_info *rexp = create_pattern(pattern.c_str());
        if (!rexp->compilation_error) {
            std::list<MachineInstance*>::iterator machines = MachineInstance::begin();
            while (machines != MachineInstance::end()) {
                MachineInstance *machine = *machines++;
                if (machine && execute_pattern(rexp, machine->getName().c_str()) == 0) {
                    if (!this->machines.count(machine)) {
                        machine->publish();
                        this->machines.insert(machine);
                    }
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
        if (machine && !this->machines.count(machine)) {
            machine->publish();
            this->machines.insert(machine);
        }
    }
    std::map<std::string, Value>::const_iterator prop_iter = definition()->monitors_properties.begin();
    while (prop_iter != definition()->monitors_properties.end()) {
        const std::pair<std::string, Value> &item = *prop_iter++;
        std::list<MachineInstance*>::iterator machines = MachineInstance::begin();
        std::cout << "settingup channel: searching for machines where " <<item.first << " == " << item.second << "\n";
        while (machines != MachineInstance::end()) {
            MachineInstance *machine = *machines++;
            if (machine && !this->machines.count(machine)) {
                Value &val = machine->getValue(item.first);
                // match if the machine has the property and Null was given as the match value
                //  or if the machine has the property and it matches the provided value
                if ( val != SymbolTable::Null &&
                        (item.second == SymbolTable::Null || val == item.second) ) {
                    std::cout << "found match " << machine->getName() <<"\n";
                    this->machines.insert(machine);
                    machine->publish();
                }
            }
        }
    }
    
    iter = definition()->ignores_patterns.begin();
    while (iter != definition()->ignores_patterns.end()) {
        const std::string &pattern = *iter++;
        rexp_info *rexp = create_pattern(pattern.c_str());
        if (!rexp->compilation_error) {
            std::list<MachineInstance*>::iterator machines = MachineInstance::begin();
            while (machines != MachineInstance::end()) {
                MachineInstance *machine = *machines++;
                if (machine && execute_pattern(rexp, machine->getName().c_str()) == 0) {
                    if (this->machines.count(machine)) {
						std::cout << "unpublished " << machine->getName() << "\n";
                        machine->unpublish();
                        this->machines.erase(machine);
                    }
                }
            }
        }
        else {
            MessageLog::instance()->add(rexp->compilation_error);
            std::cerr << "Channel error: " << definition()->name << " " << rexp->compilation_error << "\n";
        }
    }
}

