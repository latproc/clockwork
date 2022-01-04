#include "MachineShadowInstance.h"
#include "DebugExtra.h"
#include "Logger.h"

MachineShadowInstance::MachineShadowInstance(InstanceType instance_type)
    : MachineInstance(instance_type) {
    shadow_machines.push_back(this);
}

MachineShadowInstance::MachineShadowInstance(CStringHolder name, const char *type,
                                             InstanceType instance_type)
    : MachineInstance(name, type, instance_type) {
    shadow_machines.push_back(this);
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
