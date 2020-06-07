#include <iostream>
#include <map>
#include <set>
#include <stdlib.h>
#include <string.h>
#include <boost/thread.hpp>
#include "Channel.h"
#include "ChannelImplementation.h"
#include "ChannelDefinition.h"
#include "MessageLog.h"
#include "MessageEncoding.h"
#include "value.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "ConnectionManager.h"
#include "Logger.h"
#include "ClientInterface.h"
#include "IODCommand.h"
#include "IODCommands.h"
#include "SyncRemoteStatesAction.h"
#include "DebugExtra.h"
#include "ProcessingThread.h"
#include "SharedWorkSet.h"
#include "MachineInterface.h"
#include "MachineShadowInstance.h"
#include "WaitAction.h"

std::map< std::string, ChannelDefinition* > *ChannelDefinition::all = 0;

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


ChannelDefinition::ChannelDefinition(const char *n, ChannelDefinition *prnt)
		: MachineClass(n),parent(prnt), ignore_response(false) {
    if (!all) all = new std::map< std::string, ChannelDefinition* >;
    (*all)[n] = this;
	addState("DISCONNECTED");
	addState("CONNECTED");
	addState("CONNECTING");
	addState("WAITSTART");
	addState("UPLOADING");
	addState("DOWNLOADING");
	addState("ACTIVE");
	default_state = State("DISCONNECTED");
	initial_state = State("DISCONNECTED");
	features.insert(ReportPropertyChanges);
	features.insert(ReportStateChanges);
	disableAutomaticStateChanges();
}

void ChannelDefinition::addFeature(Feature f) {
	features.insert(f);
}

void ChannelDefinition::removeFeature(Feature f) {
	features.erase(f);
}

bool ChannelDefinition::hasFeature(Feature f) const {
	return features.count(f) != 0;
}

/* find the interface definition for a machine updated by this channel */
MachineClass* ChannelDefinition::interfaceFor(const char *monitored_machine) const {
	std::map<std::string, Value>::const_iterator found = updates_names.find(monitored_machine);
	if (found != updates_names.end()) {
		return MachineClass::find((*found).second.asString().c_str());
	};
	found = shares_names.find(monitored_machine);
	if (found != shares_names.end()) {
		return MachineClass::find((*found).second.asString().c_str());
	};
	return 0;
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
	if (chn) Channel::setupCommandSockets();
    return chn;
}

Value ChannelDefinition::getValue(const char *property) {
	std::map<std::string, Value>::iterator iter = options.find(property);
	if (iter == options.end()) return SymbolTable::Null;
	return (*iter).second;
}

bool ChannelDefinition::isPublisher() const { return ignore_response; }
void ChannelDefinition::setPublisher(bool which) { ignore_response = which; }

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
					DBG_CHANNELS << "Instantiating SHADOW " << instance_name.first << " for Channel " << item.second->name << "\n";
					MachineInstance *m = MachineInstanceFactory::create(instance_name.first.c_str(),
					instance_name.second.asString().c_str(),
					MachineInstance::MACHINE_SHADOW);
					m->setDefinitionLocation("dynamic", 0);
					m->requireAuthority(item.second->authority);
					DBG_CHANNELS << "shadow " << instance_name.first
						<< " requires authority " << item.second->authority << "\n";
					MachineClass *mc = MachineClass::find(instance_name.second.asString().c_str());
					m->setProperties(mc->properties);
					m->setStateMachine(mc);
					m->setValue("startup_enabled", false);
					machines[instance_name.first] = m;
					::machines[instance_name.first] = m;
				}
				else {
					char buf[150];
					snprintf(buf, 150, "Error: no interface named %s", item.first.c_str());
					MessageLog::instance()->add(buf);
					DBG_CHANNELS << buf << "\n";
				}
			}
		}
		std::map<std::string, Value>::iterator i_shared = item.second->shares_names.begin();
		while (i_shared != item.second->shares_names.end()) {
			const std::pair<std::string, Value> &instance_name = *i_shared++;
			MachineInstance *found = MachineInstance::find(instance_name.first.c_str());
			if (!found) { // instantiate a shadow to represent this machine
				if (item.second) {
					DBG_CHANNELS << "instantiating shadow machine " << instance_name.second << "\n";
					MachineInstance *m = MachineInstanceFactory::create(instance_name.first.c_str(),
																		instance_name.second.asString().c_str(),
																		MachineInstance::MACHINE_SHADOW);
					m->setDefinitionLocation("dynamic", 0);
					m->requireAuthority(item.second->authority);
					DBG_CHANNELS << "shadow " << instance_name.first
						<< " requires authority " << item.second->authority << "\n";
					MachineClass *mc = MachineClass::find(instance_name.second.asString().c_str());
					m->setProperties(mc->properties);
					m->setStateMachine(mc);
					m->setValue("startup_enabled", false);
					machines[instance_name.first] = m;
					::machines[instance_name.first] = m;
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

    copyJSONArrayToMap(obj, "shares", defn->shares_names);
    copyJSONArrayToMap(obj, "updates", defn->updates_names);
    copyJSONArrayToSet(obj, "sends", defn->send_messages);
    copyJSONArrayToSet(obj, "receives", defn->recv_messages);
    return defn;
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
    cJSON_AddItemToObject(obj, "shares", MapToJSONArray(this->shares_names));
    cJSON_AddItemToObject(obj, "updates", MapToJSONArray(this->updates_names, "name","type"));
    cJSON_AddItemToObject(obj, "sends", StringSetToJSONArray(this->send_messages));
    cJSON_AddItemToObject(obj, "receives", StringSetToJSONArray(this->recv_messages));
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return str;
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
void ChannelDefinition::addShare(const char *nm, const char *if_nm) {
	shares_names[nm] = if_nm;
	modified();
}
void ChannelDefinition::addUpdates(const char *nm, const char *if_nm) {
    updates_names[nm] = if_nm;
    modified();
}
void ChannelDefinition::addShares(const char *nm, const char *if_nm) {
	shares_names[nm] = if_nm;
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

void ChannelDefinition::processIgnoresPatternList(std::set<std::string>::const_iterator iter,
												  std::set<std::string>::const_iterator last,
												  Channel *chn) const {
	while (iter != last) {
		const std::string &pattern = *iter++;
		DBG_CHANNELS << "setupFilters() processing pattern " << pattern << "\n";
		rexp_info *rexp = create_pattern(pattern.c_str());
		if (!rexp->compilation_error) {
			std::list<MachineInstance*>::iterator machines = MachineInstance::begin();
			while (machines != MachineInstance::end()) {
				MachineInstance *machine = *machines++;
				if (!machine) continue;
				if (execute_pattern(rexp, machine->getName().c_str()) == 0) {
					if (chn->channel_machines.count(machine)) {
						DBG_CHANNELS << "unpublished " << machine->getName() << "\n";
						machine->unpublish();
						chn->channel_machines.erase(machine);
					}
					else {
						DBG_CHANNELS << "ignore pattern " << pattern << " matches " << machine->fullName() << " but it is not monitored\n";
					}
				}
				else {
					DBG_CHANNELS << machine->getName() << " does not match " << pattern << "\n";
				}
			}
		}
		else {
			MessageLog::instance()->add(rexp->compilation_error);
			DBG_CHANNELS << "Channel error: " << name << " " << rexp->compilation_error << "\n";
		}
	}
}

