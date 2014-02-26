
#include "Plugin.h"
#include "value.h"
#include "symboltable.h"
#include "MachineInstance.h"
#include "SetStateAction.h"
#include "MessageLog.h"

void log_message(cwpi_Scope, const char *m) {
	std::cout << m << "\n";
    MessageLog::instance()->add(m);
}

int getIntValue(cwpi_Scope s, const char *property_name, long **res) {
    MachineInstance *scope = static_cast<MachineInstance*>(s);
    if (!scope) {
        MessageLog::instance()->add("getIntValue was passed a null instance from a plugin");
        return 0;
    }
    Value &val = scope->getValue(property_name);
    if (val.kind != Value::t_integer)
        return 0;
    *res = &val.iValue;
    return 1;
}

char *getStringValue(cwpi_Scope s, const char *property_name) {
    MachineInstance *scope = static_cast<MachineInstance*>(s);
    if (!scope) return 0;
    
    std::string name(property_name);
    Value &val = scope->getValue(name);
    char *res = strdup(val.asString().c_str());
    return res;
}

void setIntValue(cwpi_Scope s, const char *property_name, long new_value) {
    MachineInstance *scope = static_cast<MachineInstance*>(s);
    if (!scope) return;
    std::string name(property_name);
    scope->setValue(name, new_value);
}

void setStringValue(cwpi_Scope s, const char *property_name, const char *new_value) {
    MachineInstance *scope = static_cast<MachineInstance*>(s);
    if (!scope) return;
    
    std::string name(property_name);
    scope->setValue(name, new_value);
}

int changeState(cwpi_Scope s, const char *new_state) {
    MachineInstance *scope = static_cast<MachineInstance*>(s);
    if (!scope) {
        MessageLog::instance()->add("changeState was passed a null instance from a plugin");
        return 0;
    }
    SetStateActionTemplate ssat("SELF", new_state );
    scope->active_actions.push_front(ssat.factory(scope)); // execute this state change once all other actions are complete
    return 1;
}
char *getState(cwpi_Scope s) {
    MachineInstance *scope = static_cast<MachineInstance*>(s);
    if (!scope) {
        MessageLog::instance()->add("getState was passed a null instance from a plugin");
        return 0;
    }

    std::string state(scope->getCurrentStateString());
    return strdup(state.c_str());
}

void *getNamedScope(cwpi_Scope s, const char *name) {
    MachineInstance *scope = static_cast<MachineInstance*>(s);
    if (!scope) {
        MessageLog::instance()->add("getInstanceData was passed a null instance from a plugin");
        return 0;
    }

    MachineInstance *res = scope->lookup(name);
    if (!res) MessageLog::instance()->add("getNamedScope failed lookup");
    return res;
}

void *getInstanceData(cwpi_Scope s) {
    MachineInstance *scope = static_cast<MachineInstance*>(s);
    if (!scope) {
        MessageLog::instance()->add("getInstanceData was passed a null instance from a plugin");
        return 0;
    }
    return scope->data;
}

void setInstanceData(cwpi_Scope s, void *block) {
    MachineInstance *scope = static_cast<MachineInstance*>(s);
    if (!scope) {
        MessageLog::instance()->add("setInstanceData was passed a null instance from a plugin");
        return;
    }
    scope->data = block;
}


Plugin::Plugin(plugin_func sc, plugin_func pa, plugin_filter f)
        : state_check(0), poll_actions(0), filter(0) {
    state_check = sc;
    poll_actions = pa;
    filter = f;
}

PluginManager *PluginManager::_instance = 0;

PluginManager::PluginManager() {
    
}
PluginManager *PluginManager::instance() {
    if (!_instance) _instance = new PluginManager();
    return _instance;
}

void *PluginManager::findPlugin(const std::string name) {
    std::map<std::string, void *>::iterator found = plugins.find(name);
    if (found != plugins.end()) return (*found).second;
    return 0;
}

void PluginManager::registerPlugin(const std::string name, void *handle) {
    plugins[name] = handle;
}

/*
Value &PluginScope::getValue(std::string property) {
    MachineInstance *mi = dynamic_cast<MachineInstance*>(this);
    if (!mi) return SymbolTable::Null;
    
    return mi->getValue(property);
}

void PluginScope::setValue(const std::string &property, Value new_value) {
    MachineInstance *mi = dynamic_cast<MachineInstance*>(this);
    
    if (mi) mi->setValue(property, new_value);
}

int PluginScope::changeState(const std::string new_state) {
    MachineInstance *mi = dynamic_cast<MachineInstance*>(this);
    if (!mi) return false;
    
    SetStateActionTemplate ssat("SELF", new_state );
    mi->active_actions.push_front(ssat.factory(mi)); // execute this state change once all other actions are complete
    return false;
}

std::string PluginScope::getState() {
    MachineInstance *mi = dynamic_cast<MachineInstance*>(this);
    if (!mi) return "";
    
    return mi->getCurrentStateString();
}

*/
