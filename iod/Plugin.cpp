
#include "Plugin.h"
#include "value.h"
#include "symboltable.h"
#include "MachineInstance.h"
#include "SetStateAction.h"

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

Value &PluginScope::getValue(std::string property) {
    MachineInstance *mi = dynamic_cast<MachineInstance*>(this);
    if (!mi) return SymbolTable::Null;
    
    return mi->getValue(property);
}

void PluginScope::setValue(const std::string &property, Value new_value) {
    MachineInstance *mi = dynamic_cast<MachineInstance*>(this);
    
    if (mi) mi->setValue(property, new_value);
}

bool PluginScope::changeState(const std::string new_state) {
    MachineInstance *mi = dynamic_cast<MachineInstance*>(this);
    if (!mi) return false;
    
    SetStateActionTemplate ssat("SELF", new_state );
    mi->active_actions.push_front(ssat.factory(mi)); // execute this state change once all other actions are complete
    return false;
}

