#include "MachineShadowInstance.h"
#include "Channel.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "symboltable.h"

MachineShadowInstance::MachineShadowInstance(InstanceType instance_type)
    : MachineInstance(instance_type), has_staged_state_(false) {
    shadow_machines.push_back(this);
}

MachineShadowInstance::MachineShadowInstance(CStringHolder name, const char *type,
                                             InstanceType instance_type)
    : MachineInstance(name, type, instance_type), has_staged_state_(false) {
    shadow_machines.push_back(this);
}

void MachineShadowInstance::stageRemoteState(const std::string &state_name) {
    staged_state_ = state_name;
    has_staged_state_ = true;
}

void MachineShadowInstance::stageRemoteProperty(const std::string &name, const Value &value) {
    staged_properties_[name] = value;
}

bool MachineShadowInstance::hasStagedRemote() const {
    return has_staged_state_ || !staged_properties_.empty();
}

void MachineShadowInstance::rememberPropertyDefault(const std::string &name) {
    if (property_defaults_.count(name) || null_property_defaults_.count(name)) {
        return;
    }
    const Value cur = getValue(name);
    if (cur == SymbolTable::Null) {
        null_property_defaults_.insert(name);
    }
    else {
        property_defaults_[name] = cur;
    }
}

void MachineShadowInstance::applyStagedRemote(uint64_t authority) {
    if (!hasStagedRemote()) {
        return;
    }
    // One notification for the whole interface, then the state.
    beginDeferredPropertyNotify();
    std::map<std::string, Value>::iterator it = staged_properties_.begin();
    while (it != staged_properties_.end()) {
        rememberPropertyDefault(it->first);
        setValue(it->first, it->second, authority);
        ++it;
    }
    staged_properties_.clear();
    if (has_staged_state_ && state_machine) {
        const State *s = state_machine->findState(staged_state_);
        if (!s) {
            s = state_machine->findState(state_machine->initial_state);
        }
        if (s) {
            setState(*s, authority, false);
        }
        has_staged_state_ = false;
        staged_state_.clear();
    }
    endDeferredPropertyNotify();
}

void MachineShadowInstance::revertShadowToDefaults(uint64_t authority) {
    beginDeferredPropertyNotify();
    std::map<std::string, Value>::iterator it = property_defaults_.begin();
    while (it != property_defaults_.end()) {
        setValue(it->first, it->second, authority);
        ++it;
    }
    property_defaults_.clear();
    std::set<std::string>::iterator nit = null_property_defaults_.begin();
    while (nit != null_property_defaults_.end()) {
        setValue(*nit, SymbolTable::Null, authority);
        ++nit;
    }
    null_property_defaults_.clear();
    staged_properties_.clear();
    has_staged_state_ = false;
    staged_state_.clear();
    if (state_machine) {
        const State *initial = state_machine->findState(state_machine->initial_state);
        if (initial) {
            setState(*initial, authority, false);
        }
    }
    endDeferredPropertyNotify();
}

void MachineShadowInstance::idle() {
    MachineInstance::idle();
    return;
}

Action::Status MachineShadowInstance::setState(const State &new_state, uint64_t authority,
                                               bool resume) {
    DBG_CHANNELS << _name << " (shadow) setState(" << current_state << "->" << new_state << ")\n";
    return MachineInstance::setState(new_state, authority, resume);
}

Action::Status MachineShadowInstance::setState(const char *new_state, uint64_t authority,
                                               bool resume) {
    DBG_CHANNELS << _name << " (shadow) setState(" << new_state << ")\n";
    return MachineInstance::setState(new_state, authority, resume);
}

MachineShadowInstance::~MachineShadowInstance() { return; }
