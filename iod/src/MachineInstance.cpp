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

#include "MachineInstance.h"
#include "CallMethodAction.h"
#include "RecordClass.h"
#include "Channel.h"
#include "ControlSystemMachine.h"
#include "DebugExtra.h"
#include "Dispatcher.h"
#include "ExecuteMessageAction.h"
#include "Expression.h"
#include "FireTriggerAction.h"
#include "HandleMessageAction.h"
#include "IOComponent.h"
#include "IfCommandAction.h"
#include "Logger.h"
#include "Message.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "Plugin.h"
#include "Scheduler.h"
#include "SendMessageAction.h"
#include "StallTrace.h"
#include "State.h"
#include "WaitAction.h"
#include "cJSON.h"
#include "dynamic_value.h"
#include "options.h"
#include <algorithm>
#include <boost/thread/mutex.hpp>
#include <mutex>
#include <cassert>
#include <dlfcn.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <time.h>
#include <utility>
#include <vector>
#ifndef EC_SIMULATOR
#ifdef USE_SDO
#include "SDOEntry.h"
#endif //USE_SDO
#include "ECInterface.h"
#endif
#include "AbortAction.h"
#include "CounterRateInstance.h"
#include "MachineShadowInstance.h"
#include "ProcessingThread.h"
#include "RateEstimatorInstance.h"
#include "SharedWorkSet.h"
#include "Transition.h"

extern int num_errors;
extern std::list<std::string> error_messages;

uint64_t rate_calc_process_time = 0; // used for idle calculations for rate estimation
Value *MachineInstance::polling_delay = 0;
static std::mutex pending_state_change_mutex;

// machine idle processing will occur if this value is non zero
unsigned int MachineInstance::num_machines_with_work = 1;

unsigned int MachineInstance::total_machines_needing_check = 1;

namespace {

// EtherCAT-bound machine classes whose parameters are module + CoE index[/subindex].
bool isEtherCATBoundIOType(const std::string &type) {
    return type == "POINT" || type == "STATUS_FLAG" || type == "ANALOGINPUT" ||
           type == "ANALOGOUTPUT" || type == "DIGITALVALUE" || type == "COUNTER" ||
           type == "INPUTBIT" || type == "OUTPUTBIT" || type == "INPUTREGISTER" ||
           type == "OUTPUTREGISTER";
}

// Best-effort dig-channel wire label for Beckhoff-style one-object-per-channel
// terminals (EL18xx/EL28xx/EL20xx). Other brands / packed I/O / drives: skip.
// Future: ESI/XML product tables can replace this heuristic.
bool tryBeckhoffDigWireChannel(int64_t index, int64_t subindex, unsigned bitlen,
                               bool force_digital, int &channel_1based, bool &is_output) {
    if (subindex != 1) {
        return false;
    }
    if (!force_digital && bitlen != 0 && bitlen != 1) {
        return false;
    }
    if ((index & 0xF) != 0) {
        return false;
    }
    if (index >= 0x6000 && index <= 0x60F0) {
        channel_1based = static_cast<int>((index - 0x6000) / 0x10) + 1;
        is_output = false;
        return true;
    }
    if (index >= 0x7000 && index <= 0x70F0) {
        channel_1based = static_cast<int>((index - 0x7000) / 0x10) + 1;
        is_output = true;
        return true;
    }
    return false;
}

// Common multi-field object SI labels (EL31xx-style AI, etc.). Not a wire pin.
const char *coeSubindexFieldHint(int64_t subindex) {
    switch (subindex) {
    case 1:
        return "SI1 Underrange/Input";
    case 2:
        return "SI2 Overrange";
    case 3:
        return "SI3 Limit1";
    case 5:
        return "SI5 Limit2";
    case 7:
        return "SI7 Error";
    case 17:
        return "SI17 Value";
    case 19:
        return "SI19 Frequency/Period alt";
    default:
        return nullptr;
    }
}

std::string formatCoEIndex(int64_t index) {
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
       << (static_cast<unsigned>(index) & 0xffffu);
    return os.str();
}

// Returns true if parameters were printed in the EtherCAT-aware form.
bool describeEtherCATIOParameters(std::ostream &out, MachineInstance *mi) {
    if (!mi || !isEtherCATBoundIOType(mi->_type) || mi->parameters.empty()) {
        return false;
    }
    // Parameter layout after resolve: [0] formal "module" + real_name = module instance,
    // [1] object index (int), [2] optional subindex (int), [3+] settings/PDO/etc.
    if (mi->parameters[0].val.kind != Value::t_symbol) {
        return false;
    }
    int64_t object_index = 0;
    if (mi->parameters.size() < 2 || !mi->parameters[1].val.asInteger(object_index)) {
        return false;
    }

    const std::string module_name =
        !mi->parameters[0].real_name.empty() ? mi->parameters[0].real_name
                                            : mi->parameters[0].val.asString();

    out << "  module: " << module_name;
    if (mi->parameters[0].machine) {
        out << "  state: " << mi->parameters[0].machine->getCurrent().getName();
        if (!mi->parameters[0].machine->enabled()) {
            out << " DISABLED";
        }
    }
    out << "\n";

    int64_t object_subindex = -1;
    bool has_subindex =
        mi->parameters.size() >= 3 && mi->parameters[2].val.asInteger(object_subindex);

    out << "  CoE: " << formatCoEIndex(object_index);
    if (has_subindex) {
        out << ":" << object_subindex;
    }
    out << "\n";

    unsigned bitlen = 0;
    if (mi->io_interface) {
        bitlen = mi->io_interface->address.bitlen;
    }

    const bool digital_type = (mi->_type == "POINT" || mi->_type == "STATUS_FLAG" ||
                               mi->_type == "INPUTBIT" || mi->_type == "OUTPUTBIT");
    int ch = 0;
    bool is_output = false;
    const int64_t sub_for_wire = has_subindex ? object_subindex : 1;
    if (tryBeckhoffDigWireChannel(object_index, sub_for_wire, bitlen, digital_type, ch,
                                  is_output)) {
        // Only emit wire when this looks like a dig channel (1-bit or dig class).
        if (digital_type || bitlen == 1) {
            out << "  wire: " << module_name << " ch" << ch
                << (is_output ? " (output)" : " (input)") << "\n";
        }
    }
    else if (has_subindex && (object_index & 0xF) == 0 && object_index >= 0x6000 &&
             object_index <= 0x60F0) {
        // Channel-group object (AI etc.): show channel + field, not a dig wire pin.
        const int ai_ch = static_cast<int>((object_index - 0x6000) / 0x10) + 1;
        const char *field = coeSubindexFieldHint(object_subindex);
        out << "  field: ch" << ai_ch;
        if (field) {
            out << " " << field;
        }
        else {
            out << " SI" << object_subindex;
        }
        out << "\n";
    }
    else if (has_subindex && (object_index & 0xF) == 0 && object_index >= 0x7000 &&
             object_index <= 0x70F0 && !digital_type) {
        const int ao_ch = static_cast<int>((object_index - 0x7000) / 0x10) + 1;
        out << "  field: ch" << ao_ch << " SI" << object_subindex << "\n";
    }
    // else: leave undecoded (drives, packed I/O, vendor OD) — CoE line is enough.
    // Future: product-code / ESI-XML table can supply names here.

    // Extra parameters (settings machines, optional PDO index, …).
    for (unsigned int i = 3; i < mi->parameters.size(); ++i) {
        Value p_i = mi->parameters[i].val;
        if (p_i.kind == Value::t_symbol) {
            out << "  parameter " << (i + 1) << " " << p_i.sValue << " ("
                << mi->parameters[i].real_name << "), state: "
                << (mi->parameters[i].machine ? mi->parameters[i].machine->getCurrent().getName()
                                             : "")
                << (mi->parameters[i].machine && !mi->parameters[i].machine->enabled() ? " DISABLED"
                                                                                       : "")
                << "\n";
        }
        else {
            int64_t iv = 0;
            if (p_i.asInteger(iv) && iv > 0xff) {
                // Likely another CoE/PDO index — show hex.
                out << "  parameter " << (i + 1) << " " << formatCoEIndex(iv) << " ("
                    << mi->parameters[i].real_name << ")\n";
            }
            else {
                out << "  parameter " << (i + 1) << " " << p_i << " (" << mi->parameters[i].real_name
                    << ")\n";
            }
        }
    }
    out << "\n";
    return true;
}

} // namespace

class MachineEvent {
  public:
    MachineInstance *mi;
    Message *msg;
    MachineEvent(MachineInstance *, Message *);
    MachineEvent(MachineInstance *, const Message &);
    ~MachineEvent();

  private:
    MachineEvent(const MachineEvent &);
    MachineEvent &operator=(const MachineEvent &);
};

class MachineInstance::SharedCache {
  public:
    MachineInstance *system;

    SharedCache() : system(0) {}

  private:
    SharedCache(const SharedCache &other);
    SharedCache &operator=(const SharedCache &other);
};
MachineInstance::SharedCache *MachineInstance::shared = 0;

class MachineInstance::Cache {
  public:
    std::string *full_name;
    std::string *modbus_name;
    bool reported_error;
    bool needs_throttle;

    Cache() : full_name(0), modbus_name(0), reported_error(false), needs_throttle(false) {}
    ~Cache() {
        delete full_name;
        delete modbus_name;
    }
};
std::ostream &operator<<(std::ostream &out, const ActionTemplate &a) { return a.operator<<(out); }

std::map<std::string, MachineInstance *> machines;
// std::map<std::string, MachineClass *> machine_classes;

// All machine instances automatically join and leave this list.
// During the poll process, all machines in this list have their idle() called.
std::list<MachineInstance *> MachineInstance::all_machines;
std::list<MachineInstance *> MachineInstance::record_instances;
std::list<MachineInstance *> MachineInstance::command_clocks;
std::list<MachineInstance *> MachineInstance::io_modules;
std::list<MachineInstance *> MachineInstance::automatic_machines;
std::list<MachineInstance *> MachineInstance::active_machines;
std::list<MachineInstance *> MachineInstance::shadow_machines;
std::set<MachineInstance *> MachineInstance::plugin_machines;
std::list<Package *> MachineInstance::pending_events;
std::set<MachineInstance *> MachineInstance::pending_state_change;
std::map<std::string, HardwareAddress> MachineInstance::hw_names;
std::mutex global_lists_mutex;

// The above lists need to be reorganised.. in the meantime
// this is a temporary list of machines that need to be removed
ThreadSafeList<MachineInstance *> MachineInstance::to_remove;
ThreadSafeList<MachineInstance *> MachineInstance::to_delete;

/* Factory methods */

MachineInstance *MachineInstanceFactory::create(CStringHolder name, const std::string &type,
                                                MachineInstance::InstanceType instance_type) {
    if (instance_type == MachineInstance::MACHINE_SHADOW) {
        return new MachineShadowInstance(name, type.c_str(), MachineInstance::MACHINE_INSTANCE);
    }
    if (type == "COUNTERRATE") {
        return new CounterRateInstance(name, type.c_str(), instance_type);
    }
    else if (type == "RATEESTIMATOR") {
        return new RateEstimatorInstance(name, type.c_str(), instance_type);
    }
    else {
        MachineClass *cls = MachineClass::find(type.c_str());
        ChannelDefinition *defn = dynamic_cast<ChannelDefinition *>(cls);
        if (defn) {
            // TODO: Shouldn't this use Channel::create?
            Channel *chn = new Channel(name.get(), type);
            chn->properties.add(defn->getProperties()); // load default properties
            chn->setDefinition(defn);
            return chn;
        }
        return new MachineInstance(name, type.c_str(), instance_type);
    }
}

void MachineInstance::beginDeferredPropertyNotify() { ++property_notify_defer; }

void MachineInstance::endDeferredPropertyNotify() {
    if (property_notify_defer > 0) {
        --property_notify_defer;
    }
    if (property_notify_defer == 0 && deferred_property_notify) {
        deferred_property_notify = false;
        setNeedsCheck();
        notifyDependents();
    }
}

void MachineInstance::setNeedsCheck() {
    DBG_M_MESSAGING << _name << "::setNeedsCheck(), enabled: " << is_enabled
                    << " has state machine? " << ((state_machine) ? "yes" : "no") << "\n";
    if (!getStateMachine()) {
        return;
    }
    if (!is_enabled) {
        return;
    }
    // Already queued for a check: bump the counter only. TIMER predicates that
    // are slightly overdue (t >= -2000) used to call setNeedsCheck every
    // evaluation and re-activate thousands of machines → scheduler/processing
    // storm after mass enable.
    if (needs_check > 0 &&
        (ProcessingThread::is_pending(this) || queuedForStableStateTest() ||
         !active_actions.empty() || !mail_queue.empty())) {
        ++needs_check;
        return;
    }
    if (!needs_check) {
        DBG_AUTOSTATES << _name << " needs check\n";
        ++total_machines_needing_check;
    }
    ++needs_check;
    next_poll = 0;

    // Quiet LIST (propagate_member_checks:false): member state/property noise
    // should not queue the LIST or its dependents. Enable/disable of members
    // still wakes dependents via enable()/disable() → propagateNeedsCheckToDependents.
    // empty/nonempty still updates via setState in add/removeParameter.
    if (state_machine->token_id == ClockworkToken::LIST && !listPropagatesMemberChecks()) {
        return;
    }

    if (!active_actions.empty() || !mail_queue.empty()) {
        DBG_M_MESSAGING << _name << " queued for action processing\n";
        SharedWorkSet::instance()->add(this);
        ProcessingThread::activate(this);
    }
    else if (getStateMachine()->allow_auto_states) {
        {
            std::lock_guard<std::mutex> lock(pending_state_change_mutex);
            DBG_M_MESSAGING << _name << " queued for stable state checks\n";
            pending_state_change.insert(this);
        }
        ProcessingThread::activate(this);
    }
    else {
        DBG_M_MESSAGING << _name << " queued for action processing\n";
        SharedWorkSet::instance()->add(this);
        ProcessingThread::activate(this);
    }
    if (state_machine->token_id == ClockworkToken::LIST) {
        // Full cascade (default): wake LIST dependents for ALL/ANY state rules.
        propagateNeedsCheckToDependents();
    }
    else if (state_machine->token_id == ClockworkToken::REFERENCE) {
        propagateNeedsCheckToDependents();
    }
}

std::string &MachineInstance::fullName() const {
    if (cache->full_name) {
        return *cache->full_name;
    }
    std::string res = _name;
    MachineInstance *o = owner;
    while (o) {
        res = o->getName() + "." + res;
        o = o->owner;
    }
    cache->full_name = new std::string(res);
    return *cache->full_name;
}

std::string MachineInstance::modbusName(const std::string &property, const Value &property_val) {
    std::string res;
    if (cache->modbus_name) {
        res = *cache->modbus_name;
    }
    else {
        if (owner) {
            res = owner->getName();
            res += ".";
        }
        res += _name;
        cache->modbus_name = new std::string(res);
    }
    if (property_val.token_id != ClockworkToken::tokVALUE) {
        res += ".";
        res += property;
    }
    return res;
}

bool MachineInstance::isShadow() {
    if (my_instance_type == MACHINE_SHADOW) {
        return true;
    }
    return false;
}

Channel *MachineInstance::ownerChannel() { return owner_channel; }

void MachineInstance::setNeedsThrottle(bool which) { cache->needs_throttle = which; }

bool MachineInstance::needsThrottle() { return cache->needs_throttle; }

void MachineInstance::enqueueAction(Action *a) {
    assert(a);
    active_actions.push_front(a);
    num_machines_with_work++;
    DBG_ACTIONS << _name << " New Action queued: " << *a << "\n";
    has_work = true;
    SharedWorkSet::instance()->add(this);
    ProcessingThread::activate(this);
}

void MachineInstance::enqueue(const Package &package) {
    Receiver::enqueue(package);
    setNeedsCheck();
}

bool MachineInstance::workToDo() {
    if (!pending_events.empty()) {
        DBG_MSG << "!pending_events.empty()\n";
    }
    if (!SharedWorkSet::instance()->empty()) {
        DBG_MSG << "!SharedWorkSet::instance()->empty()\n";
    }
    bool pending_empty = true;
    {
        std::lock_guard<std::mutex> lock(pending_state_change_mutex);
        if (!pending_state_change.empty()) {
            pending_empty = false;
            DBG_MSG << "!pending_state_change.empty()\n";
        }
    }
    if (num_machines_with_work + total_machines_needing_check > 0) {
        DBG_MSG << "num_machines_with_work + total_machines_needing_check > 0 ("
                << num_machines_with_work << "," << total_machines_needing_check << ") > 0\n";
    }
    return !pending_events.empty() || !SharedWorkSet::instance()->empty() ||
           !pending_empty ||
           num_machines_with_work + total_machines_needing_check > 0;
}
std::list<Package *> &MachineInstance::pendingEvents() { return pending_events; }
std::set<MachineInstance *> &MachineInstance::pluginMachines() { return plugin_machines; }

void MachineInstance::forceIdleCheck() {
    num_machines_with_work++;
    DBG_AUTOSTATES << " forced an idle check " << num_machines_with_work << "\n";
}

#if 0
void MachineInstance::add_io_entry(const char *name, unsigned int io_offset, unsigned int bit_offset)
{
    HardwareAddress addr(io_offset, bit_offset);
    addr.io_offset = io_offset;
    addr.io_bitpos = bit_offset;
    hw_names[name] = addr;
}
#endif

#if 0
bool TriggeredAction::active()
{
    std::set<IOComponent *>::iterator iter = trigger_on.begin();
    while (iter != trigger_on.end()) {
        IOComponent *ioc = *iter++;
        if (!ioc->isOn()) {
            return false;
        }
    }
    iter = trigger_off.begin();
    while (iter != trigger_off.end()) {
        IOComponent *ioc = *iter++;
        if (!ioc->isOff()) {
            return false;
        }
    }
    return true;
}
#endif

void MachineInstance::triggerFired(Trigger *trig) {
    static const std::string str_publish("publish");
    if (trig->matches(str_publish)) {
        Channel::sendPropertyChanges(this);
    }
    setNeedsCheck();
}

void MachineInstance::checkActions() {
    num_machines_with_work++;
    has_work = true;
    SharedWorkSet::instance()->add(this);
    ProcessingThread::activate(this);
    DBG_AUTOSTATES << _name << " ADDED to machines with work " << SharedWorkSet::instance()->size()
                   << "\n";
}

void MachineInstance::resetTemporaryStringStream() {
    ss.clear();
    ss.str("");
}

MachineInstance *MachineInstance::lookup(Parameter &param) {
    if (param.val.kind == Value::t_symbol) {
        if (!param.machine) {
            param.machine = lookup(param.real_name);
        }
        return param.machine;
    }
    else {
        return 0;
    }
}

MachineInstance *MachineInstance::lookup(Value &val) {
    if (!val.cached_machine && (val.kind == Value::t_symbol || val.kind == Value::t_string)) {
        val.cached_machine = lookup(val.sValue);
    }
    return val.cached_machine;
}

bool MachineInstance::uses(MachineInstance *other) {
    if (dependsOn(other)) {
        return true;
    }
    if (other->dependsOn(this)) {
        return false;
    }
    if (_type == other->_type) {
        return other->_name < _name;
    }
    if (other->_type == "MODULE") {
        return true;
    }
    if (_type == "MODULE") {
        return false;
    }
    if (other->_type == "POINT") {
        return true;
    }
    if (other->_type == "ANALOGINPUT") {
        return true;
    }
    if (other->_type == "COUNTERRATE") {
        return true;
    }
    if (other->_type == "COUNTER") {
        return true;
    }
    if (other->_type == "DIGITALVALUE") {
        return true;
    }
    if (other->_type == "COUNTERRATE") {
        return true;
    }
    if (other->_type == "ANALOGOUTPUT") {
        return true;
    }
    if (other->_type == "STATUS_FLAG") {
        return true;
    }
    if (other->_type == "FLAG") {
        return true;
    }
    if (_type == "POINT") {
        return false;
    }
    if (_type == "ANALOGINPUT") {
        return true;
    }
    if (_type == "COUNTER") {
        return true;
    }
    if (_type == "DIGITALVALUE") {
        return true;
    }
    if (_type == "COUNTERRATE") {
        return true;
    }
    if (_type == "ANALOGOUTPUT") {
        return true;
    }
    if (_type == "STATUS_FLAG") {
        return true;
    }
    if (_type == "FLAG") {
        return false;
    }
    return other->_name < _name;
}

// return true if a depends on b
bool machine_dependencies(MachineInstance *a, MachineInstance *b) { return a->uses(b); }

// return true if b depends on a
bool reverse_machine_dependencies(MachineInstance *a, MachineInstance *b) { return b->uses(a); }

void MachineInstance::sort() {
#if 0
    std::vector<MachineInstance *> tmp(all_machines.size());
    std::copy(all_machines.begin(), all_machines.end(), tmp.begin());
    //  std::sort(tmp.begin(), tmp.end(), machine_dependencies);
    all_machines.clear();
    std::copy(tmp.begin(), tmp.end(), back_inserter(all_machines));

    DBG_PARSER << "Sorted machines (dependencies)\n";
    for ((MachineInstance * m : all_machines) {
        if (m->owner) {
            DBG_PARSER << m->owner->getName() << ".";
        }
        DBG_PARSER << m->getName() << "\n";
    }
#endif
}

MachineInstance *MachineInstance::lookup(const char *name) {
    std::string seek_machine_name(name);
    return lookup(seek_machine_name);
}

MachineInstance *MachineInstance::lookup(const std::string &seek_machine_name) {
    std::map<std::string, MachineInstance *>::iterator iter =
        localised_names.find(seek_machine_name);
    if (iter != localised_names.end()) {
        return (*iter).second;
    }
    return lookup_cache_miss(seek_machine_name);
}

MachineInstance *MachineInstance::lookup_cache_miss(const std::string &seek_machine_name) {

    std::string short_name(seek_machine_name);
    size_t pos = short_name.find('.');
    if (pos != std::string::npos) {
        std::string machine_name(seek_machine_name);
        machine_name.erase(pos);
        std::string extra = short_name.substr(pos + 1);
        MachineInstance *mi = lookup(machine_name);
        if (mi) {
            return mi->lookup(extra);
        }
        else {
            return 0;
        }
    }

    MachineInstance *found = 0;
    resetTemporaryStringStream();
    //ss << _name<< " cache miss lookup " << seek_machine_name << " from " << _name;
    if (seek_machine_name == "SELF" || seek_machine_name == _name) {
        //ss << "...self";
        if (state_machine->token_id == ClockworkToken::tokCONDITION && owner) {
            found = owner;
        }
        else {
            found = this;
        }
        goto cache_local_name;
    }
    else if (seek_machine_name == "OWNER") {
        if (owner) {
            found = owner;
        }
        else {
            found = this;
        }
        goto cache_local_name;
    }
    for (unsigned int i = 0; i < locals.size(); ++i) {
        Parameter &p = locals[i];
        if (p.val.kind == Value::t_symbol && p.val.sValue == seek_machine_name) {
            //ss << "...local";
            found = p.machine;
            goto cache_local_name;
        }
    }
    for (unsigned int i = 0; i < parameters.size(); ++i) {
        Parameter &p = parameters[i];
        if (p.val.kind == Value::t_symbol &&
            (p.val.sValue == seek_machine_name || p.real_name == seek_machine_name) && p.machine) {
            //ss << "...parameter";
            found = p.machine;
            goto cache_local_name;
        }
    }
    if (state_machine)
        for (unsigned int i = 0; i < state_machine->parameters.size(); ++i) {
            Parameter &p = state_machine->parameters[i];
            if (p.val.kind == Value::t_symbol &&
                (p.val.sValue == seek_machine_name || p.real_name == seek_machine_name)) {
                //ss << "...parameter";
                if (i < parameters.size() && parameters[i].machine) {
                    found = parameters[i].machine;
                    goto cache_local_name;
                }
                else {
                    if (i >= parameters.size()) {
                        char buf[200];
                        snprintf(buf, 200, "Error: machine %s does not have a parameter %d (%s)",
                                 _name.c_str(), i, seek_machine_name.c_str());
                        MessageLog::instance()->add(buf);
                        ++num_errors;
                        error_messages.push_back(buf);
                    }
                }
            }
        }
    if (machines.count(seek_machine_name)) {
        //ss << "...global";
        found = machines[seek_machine_name];
        goto cache_local_name;
    }
    return 0;

cache_local_name:

    if (found) {
        if (seek_machine_name != found->getName()) {
            DBG_M_MACHINELOOKUPS << " linked " << seek_machine_name << " to " << found->getName()
                                 << "\n";
        }
        localised_names[seek_machine_name] = found;
    }
    return found;
}
std::ostream &operator<<(std::ostream &out, const MachineInstance &m) { return m.operator<<(out); }

/* Machine instances have different ways of reporting their 'current value', depending on the type of machine */

std::ostream &MachineValue::operator<<(std::ostream &out) const {

    if (last_result != SymbolTable::Null) {
        out << machine_instance->getName() << " (" << last_result.asString() << ")";
    }
    else {
        out << local_name << " (" << ((machine_instance) ? machine_instance->getName() : "NULL");
    }
    out << ")";
    return out;
}

void MachineValue::flushCache() {
    machine_instance = 0;
    DynamicValue::flushCache();
}

class VariableValue : public DynamicValue {
  public:
    VariableValue(MachineInstance *mi) : machine_instance(mi) {}
    virtual Value &operator()(MachineInstance *m) {
        if (machine_instance->getStateMachine()->token_id == ClockworkToken::VARIABLE ||
            machine_instance->getStateMachine()->token_id == ClockworkToken::CONSTANT) {
            last_result = machine_instance->getValue("VALUE");
            return last_result;
        }
        else {
            last_result = *machine_instance->getCurrentStateVal();
            return last_result;
        }
    }
    DynamicValue *clone() const;

  protected:
    MachineInstance *machine_instance;
};

DynamicValue *MachineValue::clone() const {
    MachineValue *mv = new MachineValue(*this);
    return mv;
}

DynamicValue *VariableValue::clone() const {
    VariableValue *vv = new VariableValue(*this);
    return vv;
}

class MachineTimerValue : public DynamicValue {
  public:
    MachineTimerValue(MachineInstance *mi) : machine_instance(mi) { last_result = 0; }
    Value &operator()(MachineInstance *m) {
        assert(machine_instance);
        if (!machine_instance) {
            last_result = false;
            return last_result;
        }
        if (machine_instance->enabled()) {
            uint64_t now = microsecs();
            int64_t msecs = (now - machine_instance->start_time) / 1000;
            last_result = msecs;
        }
        return last_result;
    }
    Value &operator()() { return operator()(machine_instance); }
    std::ostream &operator<<(std::ostream &out) const {
        return out << machine_instance->getName() << ".TIMER (" << last_result << ")";
    }
    Value *getLastResult() { return &last_result; }
    void resume() { machine_instance->start_time = microsecs() - last_result.iValue * 1000; }
    DynamicValue *clone() const;

  protected:
    MachineInstance *machine_instance;
    friend class MachineInstance;
};

DynamicValue *MachineTimerValue::clone() const {
    MachineTimerValue *mtv = new MachineTimerValue(*this);
    return mtv;
}

MachineInstance::MachineInstance(InstanceType instance_type)
    : Receiver(""), _type("Undefined"), io_interface(0), mq_interface(0), owner(0), needs_check(0),
      uses_timer(false), my_instance_type(instance_type), state_change(0), state_machine(0),
      current_state("undefined"), is_enabled(false), state_timer(0), locked(0),
      modbus_exported(ModbusExport::none), saved_state("undefined"), current_state_val("undefined"),
      is_active(false), current_value_holder(0), last_state_evaluation_time(0),
      stable_states_stats("StableState processing"), message_handling_stats("Message handling"),
      data(0), idle_time(0), next_poll(0), is_traceable(false), published(0), action_errors(0),
      owner_channel(0), cache(0), cached_notify_period_ms(1000), cached_notify_phase_ms(0),
      cached_clock_guard(0), command_clock_cache_valid(false), command_clock_index(0),
      command_fanout_skip(0), command_fanout_pending(false), property_notify_defer(0),
      deferred_property_notify(false), record_apply_mode(false), expected_authority(0) {
    if (!shared) {
        shared = new SharedCache;
    }
    cache = new Cache;
    state_timer.setDynamicValue(new MachineTimerValue(this));
    if (_type != "LIST" && _type != "REFERENCE") {
        current_value_holder.setDynamicValue(new MachineValue(this, _name));
    }
    {
        std::unique_lock<std::mutex> lock(global_lists_mutex);
        if (_type == "MODULE") { io_modules.push_back(this); }
    }
    // Initialise machines but skip shadows and templates (local machines and CONDITIONs)
    if (instance_type == MACHINE_INSTANCE) {
        std::unique_lock<std::mutex> lock(global_lists_mutex);
        all_machines.push_back(this);
        registerCommandClockLocked();
        Dispatcher::instance()->addReceiver(this);
        start_time = microsecs();
    }
    auto found = MachineClass::machine_classes.find(_type);
    if (found != MachineClass::machine_classes.end()) {
        state_machine = found->second;
    }
}

MachineInstance::MachineInstance(const CStringHolder name, const char *type,
                                 InstanceType instance_type)
    : Receiver(name), _type(type), io_interface(0), mq_interface(0), owner(0), needs_check(0),
      uses_timer(false), my_instance_type(instance_type), state_change(0), state_machine(0),
      current_state("undefined"), is_enabled(false), state_timer(0), locked(0),
      modbus_exported(ModbusExport::none), saved_state("undefined"), current_state_val("undefined"),
      is_active(false), current_value_holder(0), last_state_evaluation_time(0),
      stable_states_stats("StableState processing"), message_handling_stats("Message handling"),
      data(0), idle_time(0), is_traceable(false), published(0), action_errors(0), owner_channel(0),
      cache(0), cached_notify_period_ms(1000), cached_notify_phase_ms(0),
      cached_clock_guard(0), command_clock_cache_valid(false), command_clock_index(0),
      command_fanout_skip(0), command_fanout_pending(false), property_notify_defer(0),
      deferred_property_notify(false), record_apply_mode(false), expected_authority(0) {
    if (!shared) {
        shared = new SharedCache;
    }
    cache = new Cache;
    state_timer.setDynamicValue(new MachineTimerValue(this));
    if (_type != "LIST" && _type != "REFERENCE") {
        current_value_holder.setDynamicValue(new MachineValue(this, _name));
    }
    {
        std::unique_lock<std::mutex> lock(global_lists_mutex);
        if (_type == "MODULE") { io_modules.push_back(this); }
    }
    // Initialise machines but skip shadows and templates (local machines and CONDITIONs)
    if (instance_type == MACHINE_INSTANCE) {
        std::unique_lock<std::mutex> lock(global_lists_mutex);
        all_machines.push_back(this);
        registerCommandClockLocked();
        Dispatcher::instance()->addReceiver(this);
        start_time = microsecs();
    }
    auto found = MachineClass::machine_classes.find(_type);
    if (found != MachineClass::machine_classes.end()) {
        state_machine = found->second;
    }
}

void MachineInstance::unregisterRecord() {
    std::unique_lock<std::mutex> lock(global_lists_mutex);
    record_instances.remove(this);
    all_machines.remove(this);
}

MachineInstance::~MachineInstance() {
    {
        std::unique_lock<std::mutex> lock(global_lists_mutex);
        all_machines.remove(this);
        record_instances.remove(this);
        command_clocks.remove(this);
        automatic_machines.remove(this);
        active_machines.remove(this);
        Dispatcher::instance()->removeReceiver(this);
        delete cache;
    }
    // Free per-instance command handlers allocated in setStateMachine().
    // receives_functions: new without retain (refs == 1); enter_functions and
    // commands: new + retain (refs == 2).
    for (auto &p : receives_functions) {
        p.second->release();
    }
    receives_functions.clear();
    for (auto &p : enter_functions) {
        p.second->release();
        p.second->release();
    }
    enter_functions.clear();
    for (auto &p : commands) {
        p.second->release();
        p.second->release();
    }
    commands.clear();
}

void MachineInstance::remove_pending() {
    if (to_remove.is_empty()) { return; }

    // Drain to_remove into a local vector so we do not hold list locks while
    // touching global_lists_mutex / pending_state_change, and so entries are
    // not left on to_remove forever (for_each alone never pops).
    // TODO: examine parameters and locals of remaining machines and
    // remove/flag affected references
    std::vector<MachineInstance*> copy_of_to_remove;
    copy_of_to_remove.reserve(8);
    MachineInstance *pending = nullptr;
    while (to_remove.try_pop_front(pending)) {
        copy_of_to_remove.push_back(pending);
    }
    while (!copy_of_to_remove.empty()) {
        MachineInstance *m = copy_of_to_remove.back();
        copy_of_to_remove.pop_back();
        // Do not call methods on m: prepare_to_remove may be used from a dtor
        // (e.g. Channel) after the object is no longer safe to touch.
        {
            std::unique_lock<std::mutex> lock(global_lists_mutex);
            all_machines.remove(m);
            record_instances.remove(m);
            command_clocks.remove(m);
            automatic_machines.remove(m);
            active_machines.remove(m);
            for (auto it = ::machines.begin(); it != ::machines.end();) {
                if (it->second == m) {
                    it = ::machines.erase(it);
                }
                else {
                    ++it;
                }
            }
            SharedWorkSet::instance()->remove(m);
        }
        {
            std::lock_guard<std::mutex> lock(pending_state_change_mutex);
            pending_state_change.erase(m);
        }
    }
}

void MachineInstance::delete_pending() {
    if (to_delete.is_empty()) { return; }
    std::vector<MachineInstance *> copy_of_to_delete;
    MachineInstance *m = nullptr;
    while (to_delete.try_pop_front(m)) {
        copy_of_to_delete.push_back(m);
    }
    // Safe point: ProcessingThread runs this between scans, so no receiver
    // iteration is in flight. The destructor removes the instance from the
    // lists and the Dispatcher.
    for (size_t i = 0; i < copy_of_to_delete.size(); ++i) {
        delete copy_of_to_delete[i];
    }
}

bool MachineInstance::isPrivateConstant() const {
    if (_type != "CONSTANT") {
        return false;
    }

    const Value &private_value = properties.lookup("private");
    bool is_private = false;
    return private_value.asBoolean(is_private) && is_private;
}

void MachineInstance::describe(std::ostream &out) {
    const bool private_constant = isPrivateConstant();
    out << "---------------\n"
        << _name << ": " << current_state.getName() << " " << " Class: " << _type
        << (enabled() ? "" : " DISABLED") << (isShadow() ? " SHADOW " : " ")
        << (isActive() ? "" : " PASSIVE") << "\n"
        << " instantiated at: " << definition_file << " line:" << definition_line << "\n";
    if (expected_authority) {
        out << "authority " << expected_authority << "\n";
    }

    if (locked) {
        out << "Locked by: " << locked->getName() << "\n";
    }
    if (parameters.size()) {
        if (!describeEtherCATIOParameters(out, this)) {
            for (unsigned int i = 0; i < parameters.size(); i++) {
                Value p_i = parameters[i].val;
                if (private_constant) {
                    out << "  parameter " << (i + 1) << " <private> ("
                        << parameters[i].real_name << ")\n";
                }
                else if (p_i.kind == Value::t_symbol) {
                    out << "  parameter " << (i + 1) << " " << p_i.sValue << " ("
                        << parameters[i].real_name << "), state: "
                        << (parameters[i].machine ? parameters[i].machine->getCurrent().getName()
                                                 : "")
                        << (parameters[i].machine && !parameters[i].machine->enabled() ? " DISABLED"
                                                                                       : "")
                        << "\n";
                }
                else {
                    out << "  parameter " << (i + 1) << " " << p_i << " ("
                        << parameters[i].real_name << ")\n";
                }
            }
            out << "\n";
        }
    }
    if (io_interface) {
        out << " io: " << *io_interface << "\n";
    }
    if (locals.size()) {
        out << "Locals:\n";
        for (unsigned int i = 0; i < locals.size(); ++i) {
            if (locals[i].machine) {
                out << "  " << short_form_value(locals[i].val) << " (" << locals[i].machine->getName()
                    << "):   " << locals[i].machine->getCurrent().getName() << "\n";
            }
            else {
                out << locals[i].val << "  Missing machine\n";
            }
        }
    }
    if (modbus_exports.size()) {
        out << "Exports:\n  ";
        std::map<std::string, ModbusAddress>::const_iterator iter = modbus_exports.begin();
        while (iter != modbus_exports.end()) {
            out << (*iter++).first;
            if (iter != modbus_exports.end()) {
                out << ",";
            }
        }
        out << "\n";
    }
    if (published) {
        out << "  published (" << published << ")\n";
    }
    if (listens.size()) {
        out << "Listening to: \n";
        std::set<Transmitter *>::iterator iter = listens.begin();
        while (iter != listens.end()) {
            Transmitter *t = *iter++;
            MachineInstance *machine = dynamic_cast<MachineInstance *>(t);
            if (machine) {
                out << "  " << machine->getName() << "[" << machine->getId() << "]" << ":   "
                    << (machine->enabled() ? machine->getCurrent().getName() : "DISABLED");
                if (machine->owner) {
                    out << " owner: " << (machine->owner ? machine->owner->getName() : "null");
                }
                out << "\n";
            }
            else if (t) {
                out << "  " << t->getName() << "\n";
            }
        }
        out << "\n";
    }
    if (depends.size()) {
        out << "Dependant machines: \n";
        std::set<MachineInstance *>::iterator iter = depends.begin();
        while (iter != depends.end()) {
            MachineInstance *machine = *iter++;
            if (machine) {
                out << "  " << machine->getName() << "[" << machine->getId() << "]" << ":   "
                    << (machine->enabled() ? machine->getCurrent().getName() : "DISABLED");
                if (machine->owner) {
                    out << " owner: " << (machine->owner ? machine->owner->getName() : "null");
                }
                out << "\n";
            }
            else {
                out << " NULL\n";
            }
        }
        out << "\n";
    }
    if (keep_statistics()) {
        out << "Statistics\n";
        stable_states_stats.report(out);
        message_handling_stats.report(out);
        out << "\n";
    }
    if (state_machine && !state_machine->commands.empty()) {
        out << "Commands:\n";
        std::map<std::string, MachineCommandTemplate *>::iterator cmd_iter =
            state_machine->commands.begin();
        const char *delim = "";
        while (cmd_iter != state_machine->commands.end()) {
            const std::pair<std::string, MachineCommandTemplate *> &curr = *cmd_iter++;
            out << delim << curr.first;
            if (curr.second->switch_state) {
                const char *within_state = curr.second->getStateName().get();
                if (within_state && *within_state) {
                    out << " AUTO SWITCH to " << within_state;
                }
            }
            else {
                const auto &states = curr.second->getWithinStates();
                if (!states.empty()) {
                    out << " WITHIN ";
                    for (size_t i = 0; i < states.size(); ++i) {
                        if (i) {
                            out << ", ";
                        }
                        out << states[i];
                    }
                }
                if (curr.second->getGuard() && curr.second->getGuard()->predicate) {
                    out << " WHEN " << *curr.second->getGuard()->predicate;
                }
            }
            delim = ", ";
        }
        out << "\n";
    }

    if (!mail_queue.empty()) {
        boost::mutex::scoped_lock lock(q_mutex);
        out << _name << " queued messages: " << mail_queue.size() << "\n";
        std::list<Package>::iterator evt_iter = mail_queue.begin();
        while (evt_iter != mail_queue.end()) {
            const Package &pkg = *evt_iter++;
            out << pkg << "\n";
        }
    }

    if (action_errors) {
        out << "action errors seen: " << action_errors << "\n";
    }
    if (!active_actions.empty()) {
        out << "active actions: (" << active_actions.size() << ")\n";
        displayActive(out);
        out << "\n";
    }
    if (properties.size()) {
        out << "properties:\n  ";
        bool redact_any = private_constant;
        if (!redact_any && state_machine) {
            SymbolTableConstIterator probe = properties.begin();
            while (probe != properties.end()) {
                if (state_machine->propertyIsPrivate((*probe).first)) {
                    redact_any = true;
                    break;
                }
                ++probe;
            }
        }
        if (!redact_any) {
            out << properties;
        }
        else {
            SymbolTableConstIterator property = properties.begin();
            const char *separator = "";
            while (property != properties.end()) {
                const std::pair<std::string, Value> item = *property++;
                out << separator << item.first << ": ";
                if ((private_constant && item.first == "VALUE") ||
                    (state_machine && state_machine->propertyIsPrivate(item.first))) {
                    out << "<private>";
                }
                else {
                    out << item.second;
                }
                separator = ", ";
            }
        }
        out << "\n\n";
    }
    if (locked) {
        out << "locked: " << locked->getName() << "\n";
    }
    if (uses_timer) {
        out << "Uses timer in stable state evaluation\n";
    }
    const Value *current_timer_val = getTimerVal();
    out << "Timer: " << *current_timer_val << "\n";
    if (state_history_count > 0) {
        const StateThrashInfo thrash = analyseStateThrash();
        if (thrash.thrashing) {
            out << "*** FAST STATE THRASH (continuous; likely bad WHEN / competing stables) ***\n"
                << "  " << thrash.short_holds << "/" << thrash.history_count
                << " holds short; avg ";
            simple_deltat(out, (int64_t)thrash.avg_hold_us);
            out << " max ";
            simple_deltat(out, (int64_t)thrash.max_hold_us);
            out << " ring ";
            simple_deltat(out, (int64_t)thrash.span_us);
            out << " (~" << thrash.transitions_per_sec << "/s)\n"
                << "  path: " << thrash.path << "\n"
                << "  tip: SHOW CYCLING; plant-wide fast thrash only (ignores long-stable+brief hop)\n";
        }
        uint64_t now = microsecs();
        out << "Recent states (most recent first):\n";
        for (size_t i = 0; i < state_history_count; ++i) {
            size_t idx = (state_history_head + STATE_HISTORY_SIZE - 1 - i) % STATE_HISTORY_SIZE;
            const StateHistoryEntry &entry = state_history[idx];
            out << "  " << entry.state_name << ": entered ";
            simple_deltat(out, (int64_t)(now - entry.entered_at));
            out << " ago, held ";
            simple_deltat(out, (int64_t)(entry.left_at - entry.entered_at));
            out << "\n";
        }
    }
    if (stable_states.size() || state_machine->token_id == ClockworkToken::LIST) {
        long now_t = microsecs();
        uint64_t delta = now_t - last_state_evaluation_time;
        out << "Last stable state evaluation (";
        simple_deltat(out, delta);
        out << "):\n";
        for (unsigned int i = 0; i < stable_states.size(); ++i) {
            out << "  " << stable_states[i].state_name << ": "
                << stable_states[i].condition.last_evaluation << "\n";
            if (stable_states[i].condition.last_result == true) {
                if (stable_states[i].subcondition_handlers &&
                    !stable_states[i].subcondition_handlers->empty()) {
                    std::list<ConditionHandler>::iterator iter =
                        stable_states[i].subcondition_handlers->begin();
                    while (iter != stable_states[i].subcondition_handlers->end()) {
                        ConditionHandler &ch = *iter++;
                        out << "      " << ch.command_name() << " ";
                        if (ch.command_name() != "FLAG" && ch.trigger) {
                            out << (ch.trigger->fired() ? "fired" : "not fired");
                        }
                        out << " last: " << ch.condition.last_evaluation << "\n";
                    }
                }
                break;
            }
        }
    }
}

void MachineInstance::listenTo(MachineInstance *m) {
    if (!listens.count(m)) {
        listens.insert(m);
        for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
            StableState &s = stable_states[ss_idx];
            if (s.condition.predicate) {
                s.condition.predicate->flushCache();
            }
        }
        setNeedsCheck();
    }
}

void MachineInstance::stopListening(MachineInstance *m) {
    listens.erase(m);
    for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
        StableState &s = stable_states[ss_idx];
        if (s.condition.predicate) {
            s.condition.predicate->flushCache();
        }
    }
    setNeedsCheck();
}

bool checkDepend(std::set<Transmitter *> &checked, Transmitter *seek, Transmitter *test) {
    if (seek == test) {
        return true;
    }
    MachineInstance *mi = dynamic_cast<MachineInstance *>(test);
    if (mi) {
        BOOST_FOREACH (Transmitter *machine, mi->listens) {
            if (!checked.count(machine)) {
                checked.insert(machine);
                if (checkDepend(checked, seek, machine)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void MachineInstance::addDependancy(MachineInstance *m) {
    if (m && m != this && !depends.count(m)) {
        depends.insert(m);
        DBG_M_DEPENDANCIES << _name << " added dependant machine " << m->getName() << "\n";
    }
}

void MachineInstance::removeDependancy(MachineInstance *m) {
    if (m && m != this && depends.count(m)) {
        std::set<MachineInstance *>::iterator found = depends.find(m);
        depends.erase(found);
        DBG_M_DEPENDANCIES << _name << " removed dependant machine " << m->getName() << "\n";
    }
}

bool MachineInstance::dependsOn(Transmitter *m) {
    //  std::set<Transmitter*>checked;
    //  return checkDepend(checked, m, this);
    if (!m) {
        return false;
    }
    MachineInstance *mi = dynamic_cast<MachineInstance *>(m);
    if (!mi) {
        return true; // assume we depend on all low level transmitters
    }
    return listens.find(mi) !=
           listens.end(); // TBD - shouldn't this be checking the dependancies, not listens?
}

bool MachineInstance::needsCheck() { return needs_check != 0; }

void MachineInstance::resetNeedsCheck() { needs_check = 0; }

bool MachineInstance::listPropagatesMemberChecks() const {
    // Non-LIST: treat as "propagate" (caller should not use this for cascade).
    if (!state_machine || state_machine->token_id != ClockworkToken::LIST) {
        return true;
    }
    const Value &v = properties.lookup("propagate_member_checks");
    if (v == SymbolTable::Null) {
        return true; // default: full cascade (existing behaviour)
    }
    if (v.kind == Value::t_bool) {
        return v.bValue;
    }
    if (v.kind == Value::t_integer) {
        return v.iValue != 0;
    }
    if (v.kind == Value::t_string || v.kind == Value::t_symbol) {
        const std::string &s = v.sValue;
        if (s == "false" || s == "0" || s == "enable" || s == "enable_only") {
            return false;
        }
        if (s == "true" || s == "1" || s == "all") {
            return true;
        }
    }
    return true;
}

void MachineInstance::propagateNeedsCheckToDependents() {
    std::set<MachineInstance *>::iterator dep_iter = depends.begin();
    while (dep_iter != depends.end()) {
        MachineInstance *dep = *dep_iter++;
        if (dep->is_enabled && dep->needs_check < 2) {
            dep->setNeedsCheck();
        }
    }
}

void MachineInstance::idleReadyWork() {
    // Re-entry: COPY/PUSH during idle() mutates lists and must not nest.
    static thread_local int idle_ready_depth = 0;
    if (idle_ready_depth > 0 || !is_enabled || executingCommand()) {
        return;
    }
    ++idle_ready_depth;
    if (hasMail()) {
        idle();
        if (executingCommand()) {
            --idle_ready_depth;
            return;
        }
    }
    if (!getStateMachine() || !getStateMachine()->allow_auto_states) {
        --idle_ready_depth;
        return;
    }
    if (!needs_check && !queuedForStableStateTest()) {
        --idle_ready_depth;
        return;
    }
    if (setStableState()) {
        if (executingCommand() || hasMail()) {
            idle();
        }
    }
    --idle_ready_depth;
}

void MachineInstance::idleReadyDependents() {
    // Snapshot: idleReadyWork may add/remove depends.
    std::vector<MachineInstance *> deps(depends.begin(), depends.end());
    for (MachineInstance *dep : deps) {
        if (dep && dep != this && dep->is_enabled) {
            dep->idleReadyWork();
        }
    }
}

bool MachineInstance::queuedForStableStateTest() {
    std::lock_guard<std::mutex> lock(pending_state_change_mutex);
    return pending_state_change.count(this);
}

void MachineInstance::idle() {
    ProcessingThread::suspend(this);

    if (error_state) {
        return;
    }
    if (!is_enabled) {
        // Disabled machines never run actions/mail (see below). Unfinished
        // SetStateAction/mail would otherwise stay New forever: processAll
        // keeps re-activating them and burns processing CPU (PROCSNAP exec/mail).
        if (executingCommand() || !mail_queue.empty() || has_work) {
            clearAllActions();
            SharedWorkSet::instance()->remove(this);
        }
        return;
    }
    if (!is_active) {
        if (false && !cache->reported_error) {
            char buf[100];
            snprintf(buf, 100, "Error: %s::idle() called but this machine is not active",
                     _name.c_str());
            MessageLog::instance()->add(buf);
            cache->reported_error = true;
            if (executingCommand()) {
                snprintf(buf, 100,
                         "Error: %s::idle() machine is not active but is executing a command",
                         _name.c_str());
                MessageLog::instance()->add(buf);
            }
        }

        if (!executingCommand()) {
            return;
        }
    }
    if (!state_machine) {
        if (LOGS(DebugExtra::instance()->DEBUG_ACTIONS)) {
            std::stringstream ss;
            ss << " machine " << _name << " has no state machine";
            char *log_msg = strdup(ss.str().c_str());
            MessageLog::instance()->add(log_msg);
            DBG_M_ACTIONS << log_msg << "\n";
            free(log_msg);
        }
        return;
    }
    CaptureDuration cd(message_handling_stats);

    Action *curr = executingCommand();
    while (curr) {
        curr->retain();
        if (curr->getStatus() == Action::New || curr->getStatus() == Action::NeedsRetry) {
            //stop(curr); // avoid double queueing
            //start(curr);
            Action::Status res = (*curr)();
            if (res == Action::Failed) {
                std::stringstream ss;
                ss << _name << ": Action " << *curr << " failed: " << curr->error();
                MessageLog::instance()->add(ss.str().c_str());
                NB_MSG << ss.str() << "\n";
                has_work = true; // TODO: Need more testing aroud this.
            }
            else if (res != Action::Complete) {
                DBG_M_ACTIONS << "Action " << *curr << " is not complete, waiting...\n";
                curr->release();
                return;
            }
        }
        else if (!curr->complete()) {
            //DBG_M_ACTIONS << "Action " << *curr << " is not still not complete\n";
            WaitAction *wa = dynamic_cast<WaitAction *>(curr);
            if (wa) {
                Trigger *action_trigger = executingCommand()->getTrigger();
                if (!action_trigger->fired()) {
                    has_work = false;
                }
                else {
                    has_work = true;
                }
            }
            else {
                has_work = true;
            }
            curr->release();
            return;
        }
        Action *last = curr;
        if (last->getStatus() == Action::Failed) {
            DBG_ACTIONS << "Action " << (*curr) << " failed\n";
            ++action_errors;
        }
        curr = executingCommand();
        if (curr && curr == last) {
            DBG_M_ACTIONS << "Action " << *curr
                          << " failed to remove itself when complete. doing so manually\n";
            stop(curr);
            curr->release();
            curr = executingCommand();
            assert(curr != last);
        }
        else {
            last->release();
        }
    }
    while (!mail_queue.empty()) {
        Package *p = 0;
        {
            boost::mutex::scoped_lock lock(q_mutex);
            DBG_M_MESSAGING << _name << " has " << mail_queue.size() << " messages waiting\n";
            p = new Package(mail_queue.front());
            DBG_M_MESSAGING << _name << " found package " << *p << "\n";
            mail_queue.pop_front();
        }
        if (p) {
            handle(*p->message, p->transmitter, p->needs_receipt);
            delete p;
        }
    }
    if (mail_queue.empty() && active_actions.empty()) {
        has_work = false;
    }
    if (has_work) {
        ProcessingThread::activate(this);
    }

    return;
}
// Machine idle processing
/*
    Each machine contains a timer that indicates how long the machine has been
    in a particular state. This polling loop first ensures that the timer value
    is updated for all machines and then calles idle() on each machine.
*/

const Value *MachineInstance::getTimerVal() {
    MachineTimerValue *mtv = dynamic_cast<MachineTimerValue *>(state_timer.dynamicValue());
    mtv->operator()(this);
    //DBG_MSG << _name << "::getTimerVal " << mtv->getLastResult()->iValue << "\n";
    return mtv->getLastResult();
}

long total_machines_with_work = 0;
long total_plugins_with_work = 0;
long loop_count = 0;
long shortcuts = 0;
long total_idle_calls = 0;
long saved_loop_count = 0;
long total_process_calls = 0;
uint64_t total_processing_time = 0;
long total_aborts = 0;

bool MachineInstance::processAll(std::set<MachineInstance *> &to_process, uint32_t max_time,
                                 PollType which) {

    uint64_t start_processing = nowMicrosecs();
    rate_calc_process_time = start_processing;

    std::list<Package *>::iterator evt_iter = pending_events.begin();
    while (evt_iter != pending_events.end()) {
        Package *pkg = *evt_iter;
        evt_iter = pending_events.erase(evt_iter);
        MachineInstance *mi = dynamic_cast<MachineInstance *>(pkg->receiver);
        if (mi) {
            mi->execute(*(pkg->message), pkg->transmitter);
        }
        delete pkg;
        ++total_process_calls;
        if (mi) {
            mi->setNeedsCheck();
        }
    }

    num_machines_with_work = 0;
    rate_calc_process_time = nowMicrosecs();
    //boost::recursive_mutex &mutex(SharedWorkSet::instance()->getMutex());
    {
        //boost::recursive_mutex::scoped_lock lock(mutex);
        //std::set<MachineInstance*>::iterator busy_it = SharedWorkSet::instance()->begin();
        //while (busy_it != SharedWorkSet::instance()->end() ) {

        std::set<MachineInstance *>::iterator busy_it = to_process.begin();
        while (busy_it != to_process.end()) {
            MachineInstance *mi = *busy_it;
            // is it possible for a non active machine to be executing a command?
            if (mi->isActive() || mi->executingCommand() || !mi->mail_queue.empty()) {
                mi->idle();
                //              if (mi->enabled() && !mi->executingCommand() && mi->mail_queue.empty())
                //                  ++num_machines_with_work;
            }
            if (mi->state_machine && mi->state_machine->plugin) {
                mi->state_machine->plugin->poll_actions(mi);
            }
            Action *action = mi->executingCommand();
            if ((mi->state_machine && mi->state_machine->plugin) || (!mi->has_work && !action)) {
                if (!mi->has_work && !action) {
                    SharedWorkSet::instance()->remove(mi);
                    busy_it = to_process.erase(busy_it);
                    // Only re-queue for stable-state eval when something actually
                    // marked needs_check and the class allows auto states.
                    // Blind re-activate of every is_active machine kept runnable
                    // non-empty and spun the processing loop at POLLING_DELAY.
                    if (mi->is_active && mi->needsCheck() && mi->getStateMachine() &&
                        mi->getStateMachine()->allow_auto_states) {
                        {
                            std::lock_guard<std::mutex> lock(pending_state_change_mutex);
                            pending_state_change.insert(mi);
                        }
                        ProcessingThread::activate(mi);
                    }
                    else if (mi->needsCheck()) {
                        mi->resetNeedsCheck();
                    }
                }
                else {
                    busy_it++;
                }
            }
            else if (action) {
                if (!action->getTrigger()) { ProcessingThread::activate(mi); }
                busy_it++;
            }
            else {
                busy_it++;
            }
        }
    }

#if 0
    uint64_t now = nowMicrosecs();
    total_processing_time += now - start_processing;
    if (now - start_processing < max_time) {
        ++loop_count;    // completed a pass through all machines
    }

    if (!shared->system) {
        shared->system = MachineInstance::find("SYSTEM");
    }
    MachineInstance *system = shared->system;
    system->setValue("PENDING_STATE_CHANGE", pending_state_change.size());
    if (loop_count % 100 == 1) {
        system->setValue("AVG_PROCESS_CALLS_PER_KCYCLE", total_process_calls * 1000 / loop_count);
        system->setValue("AVG_MACHINES_WITH_WORK_PER_KCYCLE", total_machines_with_work * 1000 / loop_count);
        system->setValue("AVG_WORK_PER_KCYCLE", total_idle_calls * 1000 / loop_count);
        system->setValue("CYCLES", loop_count);
        system->setValue("AVG_PLUGIN_WORK_PER_KCYCLE", total_plugins_with_work * 1000 / loop_count);
        system->setValue("SHORTCUTS_TAKEN_PER_KCYCLE", shortcuts * 1000 / loop_count);
        system->setValue("AVG_PROCESSING_TIME_PER_KCYCLE", (long)(total_processing_time * 1000 / loop_count));
        system->setValue("AVG_ABORTS_PER_KCYCLE", total_aborts * 1000 / loop_count);
    }
#endif
    return true;
}

void MachineInstance::checkPluginStates() {
    static bool reported = false;
    //std::list<uint64_t> stats;
    //uint64_t start_processing = nowMicrosecs();
    std::set<MachineInstance *>::iterator pl_iter = plugin_machines.begin();
    while (pl_iter != plugin_machines.end()) {
        MachineInstance *m = *pl_iter++;
        if (!reported) {
            std::cerr << "----------------  Plugin in use: " << m->getName() << "\n";
        }
        if (!m->is_enabled) {
            continue;
        }
        //if (m->next_poll > start_processing) { continue; }
        //uint64_t start = nowMicrosecs();
        if (m->state_machine && m->state_machine->plugin) {
            if (m->state_machine->plugin->state_check) {
                m->state_machine->plugin->state_check(m);
            }
            if (m->state_machine->plugin->poll_actions) {
                m->state_machine->plugin->poll_actions(m);
            }
        }
        //uint64_t delta = nowMicrosecs() - start;
        //stats.push_back(delta);
    }
    reported = true;
}

// Warning: max_time is ignored in this method
bool MachineInstance::checkStableStates(std::set<MachineInstance *> &to_process,
                                        uint32_t max_time) {
    total_machines_needing_check = 0;
    std::set<MachineInstance *>::iterator iter = to_process.begin();
    while (iter != to_process.end()) {
        MachineInstance *mi = *iter++;
        // STALLSNAP breadcrumb only (no-op when DEBUG_STALLSNAP off).
        StallTrace::markMachine(mi->getName().c_str());
        if (!mi->executingCommand() && mi->mail_queue.empty()) {
            // List walkers (HMISCREEN, LIST calc, …) queue one SetState per
            // check. Stopping after that one step leaves the rest for later
            // loops (HEALTH age). Drain apply+recheck here so all ready
            // work on this machine runs in this pass. TIMER WHENs still wait.
            // Bound stops flip-flop livelock.
            if (!mi->enabled() || !mi->getStateMachine() ||
                !mi->getStateMachine()->allow_auto_states) {
                std::lock_guard<std::mutex> lock(pending_state_change_mutex);
                pending_state_change.erase(mi);
                continue;
            }
            int steps = 0;
            bool keep_pending = false;
            while (steps < 32) {
                if (Dispatcher::instance()) {
                    Dispatcher::instance()->pumpToProcessQueue();
                }
                if (ProcessingThread::instance()) {
                    ProcessingThread::instance()->drainMessageQueue();
                }
                if (mi->executingCommand() || !mi->mail_queue.empty()) {
                    mi->idle();
                }
                if (mi->executingCommand() || !mi->mail_queue.empty()) {
                    SharedWorkSet::instance()->add(mi);
                    ProcessingThread::activate(mi);
                    keep_pending = false;
                    break;
                }
                if (!mi->setStableState()) {
                    keep_pending = false;
                    break;
                }
                ++steps;
            }
            if (steps >= 32) {
                keep_pending = true;
                ProcessingThread::activate(mi);
            }
            if (!keep_pending) {
                std::lock_guard<std::mutex> lock(pending_state_change_mutex);
                pending_state_change.erase(mi);
            }
        }
        else if (mi->enabled()) {
            SharedWorkSet::instance()->add(mi);
            {
              std::lock_guard<std::mutex> lock(pending_state_change_mutex);
              pending_state_change.erase(
                mi); // this machine has other work, it should no longer be on the pending state change queue
            }
            ProcessingThread::activate(mi);
        }
    }
    return true;
}

#if 0
void MachineInstance::displayAutomaticMachines() {
    std::unique_lock<std::mutex> lock(global_lists_mutex);
    for (MachineInstance *m : automatic_machines) {
        std::cout << *m << "\n";
    }
}

void MachineInstance::displayAll() {
    std::unique_lock<std::mutex> lock(global_lists_mutex);
    for (MachineInstance *m : all_machines) {
        std::cout << "-------" << m->getName() << "\n";
        std::cout << *m << "\n";
    }
}

#endif

// linear search through the machine list, tbd performance..
MachineInstance *MachineInstance::find(const char *name) {
    std::string machine(name);
    if (machine.find('.') != std::string::npos) {
        machine.erase(machine.find('.'));
        MachineInstance *mi = find(machine.c_str());
        if (mi) {
            std::string shortname(name);
            shortname = shortname.substr(shortname.find('.') + 1);
            return mi->lookup(shortname);
        }
    }
    else {
        std::unique_lock<std::mutex> lock(global_lists_mutex);
        std::list<MachineInstance *>::iterator iter = all_machines.begin();
        while (iter != all_machines.end()) {
            MachineInstance *m = *iter++;
            if (m->_name == name) {
                return m;
            }
        }
    }
    return 0;
}

template <class T> class Inserter {
  public:
    void add(T item, std::list<T> &list, int pos, bool before) {
        if (list.empty()) {
            list.push_back(item);
        }
        else if (pos == 0 && before) {
            list.push_front(item);
        }
        else if (pos == -1 && !before) {
            list.push_back(item);
        }
        else if (pos == -1 && before) {
            typename std::list<T>::reverse_iterator iter = list.rbegin();
            if (iter != list.rend()) {
                iter++;
            }
            list.insert(iter.base(), item);
        }
        else {
            size_t n = list.size();
            if (pos > n || (pos >= n - 1 && !before)) {
                list.push_back(item);
            }
            else {
                typename std::list<T>::const_iterator iter = list.begin();
                int i = 0;
                while (iter != list.end() && i < pos) {
                    iter++;
                    i++;
                }
                if (i == pos) {
                    list.insert(iter, item);
                }
            }
        }
    }
};

void MachineInstance::addParameter(const Parameter &p, MachineInstance *mi, ssize_t position,
                                   bool before) {

    //std::cout << _name << " " << ((mi) ?  mi->getName() : "") << " " << position << " " << before << "\n";

    if (position < 0) {
        if (!before) {
            parameters.push_back(p);
        }
        else {
            parameters.insert(parameters.begin() + (parameters.size() - 1), p);
        }
    }
    else {
        if (!before) {
            position++;
        }
        if ((unsigned int)position < parameters.size()) {
            parameters.insert(parameters.begin() + position, p);
        }
        else {
            parameters.push_back(p);
        }
    }

    if (mi) {
        addDependancy(mi);
        listenTo(mi);
        mi->addDependancy(this);
        mi->listenTo(this);
    }
    if (_type == "LIST") {
        if (parameters.size() > 0 && current_state.getName() != "nonempty") {
            setState("nonempty");
        }
        updateLastEvaluationTime();
    }
    setNeedsCheck();
    notifyDependents();
    if (_type == "LIST") {
        idleReadyDependents();
    }
    //}
}

void MachineInstance::addParameter(Value param, MachineInstance *mi, ssize_t position,
                                   bool before) {
    Parameter p(param);

    if (!mi && param.kind == Value::t_symbol) {
        mi = lookup(param);
        if (mi) {
            if (!param.cached_machine) {
                param.cached_machine = mi;
            }
            p.real_name = mi->fullName();
        }
        else {
            p.val = getValue(param);
        }
    }
    else if (mi) {
        p.real_name = mi->fullName();
    }
    p.machine = mi;
    addParameter(p, mi, position, before);
}

void MachineInstance::removeParameter(size_t which) {
    if (which < 0 || which >= (size_t)parameters.size()) {
        return;
    }
    MachineInstance *m = parameters[which].machine;
    if (m) {
        m->removeDependancy(this);
        m->stopListening(this);
        removeDependancy(m);
        stopListening(m);
    }
    parameters.erase(parameters.begin() + which);
    if (_type == "LIST") {
        if (parameters.size() == 0 && current_state.getName() != "empty") {
            setState("empty");
        }
        updateLastEvaluationTime();
    }
    setNeedsCheck();
    notifyDependents();
    if (_type == "LIST") {
        idleReadyDependents();
    }
}

void MachineInstance::addLocal(Value param, MachineInstance *mi) {
    locals.push_back(param);
    locals[locals.size() - 1].machine = mi;
    if (!mi && param.kind == Value::t_symbol) {
        mi = lookup(param.asString().c_str());
    }
    std::set<MachineInstance *>::iterator dep_iter = depends.begin();
    while (dep_iter != depends.end()) {
        MachineInstance *dep = *dep_iter++;
        if (mi) {
            mi->addDependancy(dep);
            dep->listenTo(mi);
        }
        dep->setNeedsCheck();
    }

    if (mi) {
        mi->addDependancy(this);
        listenTo(mi);
        for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
            StableState &s = stable_states[ss_idx];
            if (s.condition.predicate) {
                s.condition.predicate->flushCache();
            }
        }
    }
    setNeedsCheck();
    if (_type == "LIST") {
        notifyDependents();
    }
    else if (_type == "REFERENCE") {
        setState("ASSIGNED");
        notifyDependents();
    }
}

void MachineInstance::removeLocal(int index) {
    if ((int)locals.size() <= index) {
        return;
    }
    Parameter &p = locals[index];
    if (p.machine) {
        localised_names.erase(p.machine->getName());
        stopListening(p.machine);
        p.machine->removeDependancy(this);
    }
    // each dependency of the reference should no longer be dependant on changees to the entry
    std::set<MachineInstance *>::iterator dep_iter = depends.begin();
    if (p.machine)
        while (dep_iter != depends.end()) {
            MachineInstance *dep = *dep_iter++;
            p.machine->removeDependancy(dep);
            dep->stopListening(p.machine);
            dep->setNeedsCheck();
        }
    localised_names.erase(p.val.sValue);
    std::vector<Parameter>::iterator iter = locals.begin();
    while (index-- && iter != locals.end()) {
        iter++;
    }
    if (iter != locals.end()) {
        locals.erase(iter);
    }
    setNeedsCheck();
    if (_type == "LIST") {
        notifyDependents();
    }
    else if (_type == "REFERENCE") {
        setState("EMPTY");
        notifyDependents();
    }
}

void MachineInstance::setProperties(const SymbolTable &props) {
    properties.clear();
    SymbolTableConstIterator iter = props.begin();
    while (iter != props.end()) {
        const std::pair<std::string, Value> &p = *iter;
        properties.push_back(p);
        if (p.first == "POLLING_DELAY") {
            int64_t pd;
            if (p.second.asInteger(pd)) {
                idle_time = pd;
            }
        }
        else if (p.first == "TRACEABLE") {
            if (p.second == "TRUE") {
                is_traceable = true;
            }
            if (p.second == "FALSE") {
                is_traceable = false;
            }
        }
        iter++;
    }
}

void MachineInstance::setDefinitionLocation(const char *file, int line_no) {
    definition_file = file;
    definition_line = line_no;
}

//void addReferenceLocation(const char *file, int line_no);
MachineInstance &MachineInstance::operator=(const MachineInstance &orig) {
    id = orig.id;
    _name = orig._name;
    _type = orig._type;
    parameters.clear();
    definition_file = orig.definition_file;
    definition_line = orig.definition_line;
    uses_timer = orig.uses_timer;
    std::copy(orig.parameters.begin(), orig.parameters.end(), std::back_inserter(parameters));
    std::copy(orig.locals.begin(), orig.locals.end(), std::back_inserter(locals));
    return *this;
}

std::ostream &MachineInstance::operator<<(std::ostream &out) const {
    out << _name << "(" << id << ")" << "<" << _type << ">\n";
    if (properties.begin() != properties.end()) {
        out << "  Properties: " << properties << "\n";
    }
    if (!parameters.empty()) {
        for (unsigned int i = 0; i < parameters.size(); ++i) {
            out << "  Parameter " << i << ": " << parameters[i].val
                << " real name: " << parameters[i].real_name << " ";
            if (!parameters[i].properties.empty()) {
                out << "(" << parameters[i].properties << ")";
            }
            MachineInstance *mi = parameters[i].machine;
            if (mi) {
                out << "=>" << mi->getName() << ":" << mi->id << " ";
            }
            out << "\n";
        }
    }
    if (!locals.empty()) {
        for (unsigned int i = 0; i < locals.size(); ++i) {
            out << "  Local " << i << " " << locals[i].val;
            if (!locals[i].properties.empty()) {
                out << "(" << locals[i].properties << ") ";
            }
            MachineInstance *mi = locals[i].machine;
            if (mi) {
                out << "=>" << (mi->getName()) << ":" << mi->id << " ";
            }
        }
        out << "\n";
    }
    if (uses_timer) {
        out << "Uses Timer\n";
    }
    if (!depends.empty()) {
        out << "Dependant machines: ";
        BOOST_FOREACH (MachineInstance *dep, depends)
            out << dep->getName() << ",";
        out << "\n";
    }
    out << "  Defined at: " << definition_file << ":" << definition_line << "\n";
    return out;
}

bool MachineInstance::stateExists(State &seek) {
    //return (state_machine->state_names.count(seek.getName()) != 0);

    std::list<State *>::const_iterator iter = state_machine->states.begin();
    while (iter != state_machine->states.end()) {
        if (*(*iter) == seek) {
            return true;
        }
        iter++;
    }
    for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
        if (stable_states[ss_idx].state_name == seek.getName()) {
            return true;
        }
    }
    return false;
}

/*
    Machines hear messages only when registered to do so, the rules are:
    all messages are ignored if the machine is not enabled
    all machines receive messages from themselves
    machines receive messages from objects they are listening to
    commands in the transition table are public and can be sent by anyone
*/
bool MachineInstance::receives(const Message &m, Transmitter *from) {
    if (!enabled()) {
        return false;
    }
    // passive machines do not receive messages
    //if (!is_active) return false;
    // all active machines receive messages from themselves but now we
    // check if there is a handler in the case of enter and leave messages
    // enter and leave functions are no longer automatically accepted
    if (m.isSimple() || m.isEnable()) {
        if (from == this || (io_interface && from == io_interface)) {
            DBG_M_MESSAGING << "Machine " << getName() << " receiving " << m << " from itself\n";
            return true;
        }
        // machines receive messages from objects they are listening to
        if (std::find(listens.begin(), listens.end(), from) != listens.end()) {
            DBG_M_MESSAGING << "Machine " << getName() << " receiving " << m << " from "
                            << from->getName() << "\n";
            return true;
        }
    }
    // items in the command table are public and can be sent by anyone
    if (commands.count(m.getText()) != 0) {
        return true;
    }
    // RECEIVE methods are now public also, this is necessary with the
    // current implementation of CATCH as a receive rather than a command
    // We may change this when we add visibility modifiers
    if (receives_functions.count(m)) {
        return true;
    }

    // commands in the transition table are public and can be sent by anyone
    if (m.isSimple()) {
        std::list<Transition>::const_iterator iter = transitions.begin();
        while (iter != transitions.end()) {
            const Transition &t = *iter++;
            if (t.trigger == m) {
                return true;
            }
        }
    }
    DBG_M_MESSAGING << "Machine " << getName() << " ignoring " << m << " from "
                    << ((from) ? from->getName() : "NULL") << "\n";
    if (from && dependsOn(from)) {
        DBG_M_MESSAGING << getName() << " depends on " << from->getName()
                        << " registering a check\n";
        setNeedsCheck();
    }
    return false;
}

Action::Status MachineInstance::setState(const char *sn, uint64_t authority, bool resume) {
    char buf[150];
    if (!state_machine) {
        snprintf(buf, 150, "Error: setting state to %s on a machine (%s) with no statemachine", sn,
                 _name.c_str());
        MessageLog::instance()->add(buf);
        DBG_MSG << buf << "\n";
        return Action::Failed;
    }
    const State *s = state_machine->findState(sn);
    if (!s) {
        snprintf(buf, 150, "Error: setting state to unknown value: '%s' on %s", sn, _name.c_str());
        MessageLog::instance()->add(buf);
        DBG_MSG << buf << "\n";
        return Action::Failed;
    }
    return setState(*s, authority, resume);
}

void MachineInstance::forceStableStateCheck() {
    DBG_AUTOSTATES << "stable state check forced\n";
    total_machines_needing_check++;
}

void MachineInstance::requireAuthority(uint64_t auth) { expected_authority = auth; }

uint64_t MachineInstance::requiredAuthority() { return expected_authority; }

// setup the triggers for subconditions and return the earliest time found
uint64_t MachineInstance::setupSubconditionTriggers(const StableState &s,
                                                    uint64_t u_earliestTimer) {
    int64_t earliestTimer = (int64_t)u_earliestTimer;
    int64_t result = earliestTimer;
    std::list<ConditionHandler>::iterator iter = s.subcondition_handlers->begin();
    while (iter != s.subcondition_handlers->end()) {
        ConditionHandler &ch = *iter++;
        //setup triggers for subcondition handlers

        if (ch.uses_timer) {
            int64_t timer_val;
            ch.reset();
            if (s.state_name == current_state.getName()) {
                // BUG here. If the timer comparison is '>' (ie Timer should be > the given value
                //   we should trigger at v.iValue+1
                //NB_MSG << "setting up trigger for subcondition on state " << s.state_name << "\n";

                std::list<Predicate *> timer_clauses;
                DBG_PREDICATES << "searcing: " << (*ch.condition.predicate) << "\n";
                ch.condition.predicate->findTimerClauses(timer_clauses);
                std::list<Predicate *>::iterator iter = timer_clauses.begin();
                timer_val = LONG_MAX;
                while (iter != timer_clauses.end()) {
                    Predicate *node = *iter++;
                    const Value &tv = node->getTimerValue();
                    if (tv == SymbolTable::Null) {
                        continue;
                    }
                    if (tv.kind == Value::t_symbol || tv.kind == Value::t_string) {
                        Value v = getValue(tv.sValue);
                        if (v.kind != Value::t_integer) {
                            DBG_MSG << _name << " Warning: timer value " << v << " for state "
                                    << s.state_name << " subcondition is not numeric\n";
                            continue;
                        }

                        if (v.iValue >= 0 && v.iValue < timer_val) {
                            timer_val = v.iValue;
                            if (node->op == opGT) {
                                ++timer_val;
                            }
                        }
                        else {
                            DBG_PREDICATES << "skipping large timer value " << v.iValue << "\n";
                        }
                    }
                    else if (tv.kind == Value::t_integer) {
                        if (tv.iValue < timer_val) {
                            timer_val = tv.iValue;
                            if (node->op == opGT) {
                                ++timer_val;
                            }
                        }
                        else {
                            DBG_PREDICATES << "skipping a large timer value " << tv.iValue << "\n";
                        }
                    }
                    else {
                        DBG_MSG << _name << " Warning: timer value for state " << s.state_name
                                << " subcondition is not numeric\n";
                        continue;
                    }
                }
                if (timer_val < LONG_MAX && timer_val < earliestTimer) {
                    result = timer_val;
                }
            }
        }
        else if (ch.command_name() == "FLAG") {
            if (s.state_name == current_state.getName()) {
                //TBD What was intended here?
            }
        }
    }
    return (uint64_t)result;
}

void MachineInstance::pushStateHistory(const std::string &state_name, uint64_t entered_at,
                                       uint64_t left_at) {
    if (state_name.empty() || state_name == "undefined") {
        return;
    }
    state_history[state_history_head] = {state_name, entered_at, left_at};
    state_history_head = (state_history_head + 1) % STATE_HISTORY_SIZE;
    if (state_history_count < STATE_HISTORY_SIZE) {
        ++state_history_count;
    }
}

MachineInstance::StateThrashInfo
MachineInstance::analyseStateThrash(uint64_t short_hold_us, size_t min_short,
                                    bool want_path) const {
    StateThrashInfo info;
    info.history_count = state_history_count;
    const uint64_t now = microsecs();
    if (start_time > 0 && now > start_time) {
        info.current_dwell_us = now - start_time;
    }

    // Need a full-ish ring of completed visits — true thrash fills the buffer.
    if (state_history_count < 5) {
        return info;
    }

    uint64_t sum_hold = 0;
    uint64_t oldest_enter = 0;
    uint64_t newest_leave = 0;
    info.min_hold_us = ~0ULL;

    for (size_t i = 0; i < state_history_count; ++i) {
        size_t idx = (state_history_head + STATE_HISTORY_SIZE - state_history_count + i) %
                     STATE_HISTORY_SIZE;
        const StateHistoryEntry &e = state_history[idx];
        const uint64_t hold = (e.left_at > e.entered_at) ? (e.left_at - e.entered_at) : 0;
        sum_hold += hold;
        if (hold < info.min_hold_us) {
            info.min_hold_us = hold;
        }
        if (hold > info.max_hold_us) {
            info.max_hold_us = hold;
        }
        if (hold < short_hold_us) {
            ++info.short_holds;
        }
        if (oldest_enter == 0 || e.entered_at < oldest_enter) {
            oldest_enter = e.entered_at;
        }
        if (e.left_at > newest_leave) {
            newest_leave = e.left_at;
        }
    }
    info.avg_hold_us = sum_hold / state_history_count;
    if (info.min_hold_us == ~0ULL) {
        info.min_hold_us = 0;
    }
    if (newest_leave > oldest_enter) {
        info.span_us = newest_leave - oldest_enter;
        info.transitions_per_sec =
            (double)state_history_count * 1000000.0 / (double)info.span_us;
    }

    // Continuous fast thrash only. Do NOT flag long-stable machines that take
    // an occasional short hop (CHANNELSTATUSLOCAL idle→update, scales getWeight,
    // boot INIT sequences mixed with long dwells):
    //   - almost every hold in the ring is short
    //   - average and max hold are both short (no multi-second "stable" in ring)
    //   - the whole ring was burned quickly (span)
    //   - transition rate is high
    const size_t almost_all =
        (state_history_count * 3 + 3) / 4; // ceil(0.75 * n)
    const bool almost_all_short =
        info.short_holds >= almost_all && info.short_holds >= min_short;
    const bool holds_uniformly_short =
        info.avg_hold_us < short_hold_us &&
        info.max_hold_us < short_hold_us * 4; // e.g. max < 200ms if short=50ms
    const bool ring_burned_quickly =
        info.span_us > 0 && info.span_us < 1000000ULL; // entire ring < 1s
    const bool fast_enough = info.transitions_per_sec >= 10.0;
    // Drop stale boot noise: sitting stably now means the ring is history only.
    const bool still_thrashing_now = info.current_dwell_us < 500000ULL; // < 0.5s

    info.thrashing = almost_all_short && holds_uniformly_short && ring_burned_quickly &&
                     fast_enough && still_thrashing_now;

    if (info.thrashing && want_path) {
        std::string path;
        path.reserve(state_history_count * 12);
        for (size_t i = 0; i < state_history_count; ++i) {
            size_t idx = (state_history_head + STATE_HISTORY_SIZE - state_history_count + i) %
                         STATE_HISTORY_SIZE;
            if (i > 0) {
                path += " -> ";
            }
            path += state_history[idx].state_name;
        }
        info.path = std::move(path);
    }
    return info;
}

Action::Status MachineInstance::setState(const State &new_state, uint64_t authority, bool resume) {
    if (expected_authority != 0 && authority == 0) { //expected_authority != authority ) {
        //FileLogger fl(program_name);
        //fl.f() << _name << " refused to change state to " << new_state << " due to authority mismatch. "
        //<< " needed: " << expected_authority << " got " << authority << "\n";
        if (isShadow()) {
            Channel *chn = ownerChannel();
            if (chn && chn->current_state == ChannelImplementation::ACTIVE) {
                chn->requestStateChange(this, new_state.getName(), authority);
            }
            return Action::Running;
        }
        else {
            return Action::Failed;
        }
    }
    else if (expected_authority == 0 && authority != 0) {
        //FileLogger fl(program_name);
        //fl.f() << _name << " refused to change state to " << new_state << " due to authority mismatch. "
        //<< " needed: " << expected_authority << " got " << authority << "\n";
        return Action::Failed;
    }
    if (new_state.getName() != getCurrent().getName()) {
        pushStateHistory(getCurrent().getName(), start_time, microsecs());
    }

    if (!resume && current_state == new_state) {
        return Action::Complete;
    }

    const State *machine_class_state = state_machine->findState(new_state);

    if (!machine_class_state) {
        const Value &s = getValue(new_state.getName().c_str());
        State propertyState(s.asString().c_str());
        if (!hasState(propertyState)) {
            char buf[150];
            snprintf(buf, 150, "Error: unknown state: '%s' on %s", new_state.getName().c_str(),
                     _name.c_str());
            MessageLog::instance()->add(buf);
            DBG_MSG << buf << "\n";
            return Action::Failed;
        }
        return setState(propertyState, authority, resume);
    }
    Action::Status stat = Action::Complete;

    {
#ifndef DISABLE_LEAVE_FUNCTIONS
        /* notify everyone we are leaving a state */
        // TBD use notifyDependents()

        /* do not send a leave message for the state if the state is not changing (ie in resume) */
        if (enabled() && current_state != new_state && current_state.getName() != "undefined") {
            std::string txt = current_state.getName() + "_leave";
            Message msg(txt.c_str(), Message::LEAVEMSG);
            if (receives(msg, this)) {
                stat = execute(msg, this);
                if (stat == Action::Failed || (!isActive() && stat != Action::Complete)) {
                    char buf[200];
                    snprintf(buf, 200, "%s %s leave action did not complete (%s)", _name.c_str(),
                             txt.c_str(), actionStatusName(stat));
                    MessageLog::instance()->add(buf);
                    NB_MSG << buf << "\n";
                }
            }

            if (!machine_class_state->isLocal()) {
                txt = _name + "." + current_state.getName() + "_leave";
                msg = Message(txt.c_str(), Message::LEAVEMSG);
                std::set<MachineInstance *>::iterator dep_iter = depends.begin();
                while (dep_iter != depends.end()) {
                    MachineInstance *dep = *dep_iter++;
                    if (this == dep) {
                        continue;
                    }
    
                    // note that the following test prevents us from resuming a
                    // blocked action in dep and that may be bad but the test is
                    // not used in the case of the ENTER handler, below and there
                    // is no need to resume twice
                    if (!dep->receives(msg, this)) {
                        continue;
                    }
                    Action *act = dep->executingCommand();
                    if (act) {
                        if (act->getStatus() == Action::Suspended) {
                            act->resume();
                        }
                        DBG_M_MESSAGING << dep->getName() << "[" << dep->getCurrent().getName() << "]"
                                        << " is executing " << *act << "\n";
                    }
    
                    // execute the message on the dependant machine
                    dep->execute(msg, this);
                }
            }
        }
#endif
#if 1
        // TBD Why do we send the modbus state change here. This should not be done
        // until later, if the state actually changes

        // update the Modbus interface for self
        if (published && !machine_class_state->isLocal() &&
            (modbus_exported == ModbusExport::discrete || modbus_exported == ModbusExport::coil)) {
            if (!modbus_exports.count(_name)) {
                char buf[100];
                snprintf(buf, 100, "%s Internal Error: modbus export info not found",
                         getName().c_str());
                MessageLog::instance()->add(buf);
                NB_MSG << buf << "\n";
            }
            else {
                auto is_on = [](const std::string &state) -> bool {
                    return state == "on" || state == "true";
                };
                ModbusAddress ma = modbus_exports[_name];
                if (ma.getSource() == ModbusAddress::machine &&
                    (ma.getGroup() != ModbusAddress::none)) {
                    if (is_on(new_state.getName())) { // just turned on
                        DBG_MODBUS << _name << " machine came on; triggering a modbus message\n";
                        ma.update(this, 1);
                    }
                    else if (is_on(current_state.getName())) {
                        DBG_MODBUS << _name << " machine went off; triggering a modbus message\n";
                        ma.update(this, 0);
                    }
                }
                else {
                    DBG_M_MSG << _name << ": exported: " << modbus_exported << " (" << ma
                              << ") but address is of type 'none'\n";
                }
            }
        }
#endif
        start_time = microsecs();
        disabled_time = start_time;
        DBG_STATECHANGES << fullName() << " changing from " << current_state << " to " << new_state
                         << "\n";
        std::string last = current_state.getName();
        current_state = new_state;
        current_state_val = new_state.getName();

        // call the internal enter function for the machine if available
        if (machine_class_state) {
            machine_class_state->enter(0);
        }

        properties.add("STATE", Value{current_state.getName().c_str()}, SymbolTable::ST_REPLACE);
        if (!machine_class_state->isLocal()) {
            // publish the state change if we are a publisher
            if (mq_interface) {
                if (_type == "POINT" && properties.lookup("type") == Value{"Output"}) {
                    mq_interface->publish(properties.lookup("topic").asString(), new_state.getName(),
                                          this);
                }
            }
            if (published && !modbus_exports.empty()) {
                // update the Modbus interface for the state
                if (modbus_exports.count(_name + "." + last)) {
                    ModbusAddress ma = modbus_exports[_name + "." + last];
                    DBG_MODBUS << _name << " leaving state " << last << " triggering a modbus message "
                               << ma << "\n";
                    assert(ma.getSource() == ModbusAddress::state);
                    ma.update(this, 0);
                }
                if (modbus_exports.count(_name + "." + current_state.getName())) {
                    ModbusAddress ma = modbus_exports[_name + "." + current_state.getName()];
                    DBG_MODBUS << _name << " active state " << current_state.getName()
                               << " triggering a modbus message " << ma << "\n";
                    assert(ma.getSource() == ModbusAddress::state);
                    ma.update(this, 1);
                }
            }
        }
        /* TBD optimise the timer triggers to only schedule the earliest trigger */
        StableState *earliestTimerState = NULL;
        int64_t earliestTimer = LONG_MAX;

        uint64_t stable_state_timer_base = microsecs();
        bool dbg_report_if_timer_found = false;
        for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
            StableState &s = stable_states[ss_idx];
            if (s.uses_timer) {
                dbg_report_if_timer_found = true;
                // first disable any trigger that may still be enabled
                if (s.trigger) {
                    DBG_M_SCHEDULER << _name << " clearing trigger for state " << s.state_name
                                    << "\n";
                    if (s.trigger->enabled()) {
                        DBG_M_SCHEDULER << _name << " disabling " << s.trigger->getName() << "\n";
                        s.trigger->disable();
                    }
                    s.trigger = s.trigger->release();
                }
                s.condition.predicate->clearTimerEvents(this);

                int64_t timer_val;

                // BUG here. If the timer comparison is '>' (ie Timer should be > the given value
                //   we should trigger at v.iValue+1
                if (s.timer_val.kind == Value::t_symbol || s.timer_val.kind == Value::t_string) {
                    Value v = getValue(s.timer_val.sValue);
                    if (v.kind != Value::t_integer) {
                        char buf[200];
                        snprintf(buf, 200,
                                 "%s Error: timer value for state %s is not numeric. "
                                 "timer_val: %s lookup value: %s type: %d",
                                 _name.c_str(), s.state_name.c_str(), s.timer_val.sValue.c_str(),
                                 v.asString().c_str(), v.kind);
                        MessageLog::instance()->add(buf);
                        continue;
                    }
                    else {
                        timer_val = v.iValue;
                    }
                }
                else if (s.timer_val.kind == Value::t_integer) {
                    timer_val = s.timer_val.iValue;
                }
                else if (s.timer_val.kind == Value::t_float) {
                    timer_val = trunc(s.timer_val.fValue);
                }
                else {
                    DBG_M_SCHEDULER << _name << " Warning: timer value for state " << s.state_name
                                    << " is not numeric\n";
                    DBG_M_SCHEDULER << _name << " state: " << s.state_name
                                    << " timer_val: " << s.timer_val
                                    << " type: " << s.timer_val.kind << "\n";
                    continue;
                }
                // note comment above, this is not a correct handling of opGT and opLT
                if (s.condition.predicate->op == opGT) {
                    timer_val++;
                }
                else if (s.condition.predicate->op == opLT) {
                    --timer_val;
                }

                if (timer_val < earliestTimer || earliestTimerState == 0) {
                    earliestTimerState = &s;
                    earliestTimer = timer_val;
                }
                else {
                    DBG_M_SCHEDULER << _name << " ignoring scheduled check for stable state #"
                                    << ss_idx << "(" << timer_val << ") in favour of earlier check "
                                    << earliestTimer << "\n";
                }
            }
            if (s.subcondition_handlers) {
                int64_t sc_timer;
                if ((sc_timer = (int64_t)setupSubconditionTriggers(s, earliestTimer)) <
                    earliestTimer) {
                    earliestTimerState = &s;
                    earliestTimer = sc_timer;
                }
            }
        }
        if (earliestTimerState) {
            StableState s(*earliestTimerState);
            int64_t timer_val = earliestTimer;

            DBG_M_SCHEDULER << _name << " Scheduling timer for " << timer_val << "ms\n";
            // prepare a new trigger. note: very short timers will still be scheduled
            // TBD move this outside of the loop and only apply it for the earliest timer
            std::string trigger_name("SSTimer ");
            trigger_name += _name;
            trigger_name += " ";
            trigger_name += s.state_name;
            if (s.trigger) {
                s.trigger->release();
                s.trigger = 0;
            }
            if (timer_val > 0) {
                s.trigger = new Trigger(this, trigger_name);
                //FireTriggerAction *fta = new FireTriggerAction(this, s.trigger);
                //Scheduler::instance()->add(new ScheduledItem(stable_state_timer_base, timer_val*1000, fta));
                Scheduler::instance()->add(
                    new ScheduledItem(stable_state_timer_base, timer_val * 1000, s.trigger));
            }
            else if (timer_val >= -2) {
                ProcessingThread::activate(this);
            }
        }
        else if (dbg_report_if_timer_found) {
            DBG_M_SCHEDULER << " no state timer required\n";
        }

        if (published && !machine_class_state->isLocal()) {
            /*  {
                FileLogger fl(program_name);
                fl.f() << "Sending state change for " << _name << " to " << new_state.getName() << "\n";
                }*/
            Channel::sendStateChange(this, new_state.getName(), authority);
        }

        /* notify everyone we are entering a state */

        std::string txt = _name + "." + new_state.getName() + "_enter";
        Message msg(txt.c_str(), Message::ENTERMSG);
        stat = execute(msg, this);
        if (!machine_class_state->isLocal()) {
            notifyDependents(msg);
        }
    }
    return stat;
}

void MachineInstance::notifyDependents() {
    std::set<MachineInstance *>::iterator dep_iter = depends.begin();
    while (dep_iter != depends.end()) {
        MachineInstance *dep = *dep_iter++;
        if (this == dep) {
            continue;
        }
        if (!dep->is_enabled) {
            continue;
        }
        if (!dep->is_active && !dep->io_interface) {
            continue;
        }
        DBG_M_MESSAGING << _name << " should tell " << dep->getName() << " to check state\n";
        Action *act = dep->executingCommand();
        if (act) {
            if (act->getStatus() == Action::Suspended) {
                act->resume();
            }
            DBG_M_MESSAGING << dep->getName() << "[" << dep->getCurrent().getName() << "]"
                            << " is executing " << *act << "\n";
        }
        dep->setNeedsCheck();
        //      if (dep->state_machine->token_id == ClockworkToken::LIST ) dep->notifyDependents();
    }
}

// CLOCKED* wrappers: same silent property write as IA IOTIME (properties.add,
// no per-field setValue). IOTIME always changes; using setValue here notified
// CHANNEL + dependents four times per analog per period and starved HMI/mode.
namespace {
bool replaceIfChanged(MachineInstance *dep, const char *key, const Value &v) {
    const Value &prev = dep->properties.lookup(key);
    if (prev != SymbolTable::Null && prev.identical(v) && prev.kind == v.kind) {
        return false;
    }
    dep->properties.add(key, v, SymbolTable::ST_REPLACE);
    return true;
}

void publishThinValueIfChanged(MachineInstance *dep, const Value &v) {
    if (!replaceIfChanged(dep, "VALUE", v)) {
        return;
    }
    if (dep->published) {
        Channel::sendPropertyChange(dep, "VALUE", v, 0);
    }
    dep->setNeedsCheck();
    dep->notifyDependents();
}
} // namespace

bool MachineInstance::applyThinClockedUpdate(MachineInstance *dep) {
    if (!dep || !dep->state_machine) {
        return false;
    }
    const std::string &cls = dep->state_machine->name;
    if (cls == "CLOCKEDANALOGINPUT" || cls == "CLOCKEDANALOGINPUTWITHSETTINGS") {
        replaceIfChanged(dep, "IOTIME", getValue("IOTIME"));
        replaceIfChanged(dep, "Velocity", getValue("Velocity"));
        replaceIfChanged(dep, "Acceleration", getValue("Acceleration"));
        publishThinValueIfChanged(dep, getValue("ENG"));
        return true;
    }
    if (cls == "CLOCKEDCOUTERINPUT") {
        replaceIfChanged(dep, "IOTIME", getValue("IOTIME"));
        publishThinValueIfChanged(dep, getValue("VALUE"));
        return true;
    }
    if (cls == "CLOCKEDCOUTER16BIT") {
        int64_t raw = 0;
        getValue("VALUE").asInteger(raw);
        if (raw > 32767) {
            raw -= 65536;
        }
        replaceIfChanged(dep, "IOTIME", getValue("IOTIME"));
        publishThinValueIfChanged(dep, Value(raw));
        return true;
    }
    return false;
}

void MachineInstance::notifyCommandConsumers(const char *command_name,
                                             uint64_t notify_period_ms, bool continue_fanout) {
    // Only dependants that declare the command (RECEIVE/COMMAND handlers).
    // Same interest filter idea as CHANNEL "MONITORS MACHINES LINKED TO …".
    // Per dependant: at most one matching command in flight. Fresh pending is
    // coalesced; stale pending is dropped and replaced so a stuck queue cannot
    // mute COMMANDCLOCK forever.
    // K=1: at most one eligible send per call. Remainder is drained on the
    // next poll via continue_fanout (not the next period).
    if (!command_name || !*command_name) {
        command_name = "update";
    }
    if (notify_period_ms == 0) {
        notify_period_ms = 100;
    }
    // Stale threshold: 2× notify_period, floor 50 ms (plan default K=2).
    uint64_t stale_us = notify_period_ms * 2ULL * 1000ULL;
    if (stale_us < 50000ULL) {
        stale_us = 50000ULL;
    }
    if (!continue_fanout) {
        command_fanout_skip = 0;
        command_fanout_pending = false;
    }
    Message command_msg(command_name);
    size_t idx = 0;
    size_t sent = 0;
    bool more = false;
    const size_t start = command_fanout_skip;
    for (MachineInstance *dep : depends) {
        if (idx < start) {
            ++idx;
            continue;
        }
        if (!dep || dep == this || !dep->enabled()) {
            ++idx;
            continue;
        }
        if (dep->receives_functions.find(command_msg) == dep->receives_functions.end()) {
            ++idx;
            continue;
        }
        if (!dep->acceptsCommandInCurrentState(command_name)) {
            ++idx;
            continue;
        }
        uint64_t pending_age_us = 0;
        if (dep->hasPending(command_msg, &pending_age_us)) {
            if (pending_age_us < stale_us) {
                ++idx;
                continue;
            }
            const size_t dropped = dep->dropPending(command_msg);
            DBG_MESSAGING << _name << " " << command_name << " -> " << dep->getName()
                          << " stale pending age_us=" << pending_age_us
                          << " dropped=" << dropped << " re-send\n";
        }
        // Due tick: one dependant (do not stack calcAdjust). Continue
        // poll: drain the rest in this call so every 1 ms sample is not
        // a K=1 send from every pending IA/clock.
        const size_t send_limit = continue_fanout ? static_cast<size_t>(-1) : 1;
        if (sent >= send_limit) {
            more = true;
            break;
        }
        DBG_M_MESSAGING << _name << " " << command_name << " -> " << dep->getName() << "\n";
        if (strcmp(command_name, "update") != 0 || !applyThinClockedUpdate(dep)) {
            sendMessageToReceiver(command_msg, dep, false);
        }
        ++sent;
        ++idx;
        command_fanout_skip = idx;
    }
    command_fanout_pending = more;
    if (!more) {
        command_fanout_skip = 0;
    }
}

void MachineInstance::registerCommandClockLocked() {
    if (my_instance_type != MACHINE_INSTANCE) {
        return;
    }
    const bool is_clock = (_type == "COMMANDCLOCK") ||
                          (state_machine && state_machine->name == "COMMANDCLOCK");
    if (!is_clock) {
        return;
    }
    for (MachineInstance *c : command_clocks) {
        if (c == this) {
            return;
        }
    }
    command_clock_index = command_clocks.size();
    command_clocks.push_back(this);
}

size_t MachineInstance::commandClockCount() {
    std::unique_lock<std::mutex> lock(global_lists_mutex);
    return command_clocks.size();
}

void MachineInstance::refreshCommandClockCache() {
    const Value &configured_period = getValue("notify_period");
    if (configured_period.kind == Value::t_integer && configured_period.iValue > 0) {
        cached_notify_period_ms = static_cast<uint64_t>(configured_period.iValue);
    }
    else {
        cached_notify_period_ms = 1000;
    }
    cached_command_name = "calcAdjust";
    const Value &configured_command = getValue("command");
    if (configured_command.kind == Value::t_string && !configured_command.sValue.empty()) {
        cached_command_name = configured_command.sValue;
    }
    // Optional notify_phase (ms). Missing → stagger by list index so same-period
    // clocks do not share a slot. Analog/COUNTER CommandClock callers stay phase 0.
    if (properties.exists("notify_phase")) {
        const Value &configured_phase = getValue("notify_phase");
        if (configured_phase.kind == Value::t_integer && configured_phase.iValue >= 0) {
            cached_notify_phase_ms = static_cast<uint64_t>(configured_phase.iValue);
        }
        else {
            cached_notify_phase_ms = 0;
        }
    }
    else if (cached_notify_period_ms > 0) {
        cached_notify_phase_ms = static_cast<uint64_t>(command_clock_index % cached_notify_period_ms);
    }
    else {
        cached_notify_phase_ms = 0;
    }
    cached_clock_guard = lookup("Guard");
    command_clock_cache_valid = true;
}

void MachineInstance::dispatchCommandClocks(uint64_t now_us) {
    std::unique_lock<std::mutex> lock(global_lists_mutex);

    for (MachineInstance *clock : command_clocks) {
        if (!clock) {
            continue;
        }
        IOComponent::noteClockVisit();

        if (!clock->command_clock_cache_valid) {
            clock->refreshCommandClockCache();
        }
        const uint64_t period_ms = clock->cached_notify_period_ms;
        const uint64_t phase_ms = clock->cached_notify_phase_ms;

        // Local CW state is visible to DESCRIBE; also fail-closed on Guard while a
        // transition is queued or Guard becomes disabled/off/false.
        MachineInstance *guard = clock->cached_clock_guard;
        bool enabled = strcmp(clock->getCurrentStateString(), "on") == 0;
        enabled = enabled && guard && guard->enabled();
        if (enabled) {
            const char *state = guard->getCurrentStateString();
            enabled = state && strcasecmp(state, "off") != 0 && strcasecmp(state, "false") != 0;
        }

        if (clock->hasCommandFanoutPending()) {
            clock->notifyCommandConsumers(clock->cached_command_name.c_str(), period_ms, true);
            IOComponent::noteClockSend();
        }

        if (!clock->command_clock.due(now_us, period_ms, enabled, phase_ms)) {
            continue;
        }
        IOComponent::noteClockDue();

        DBG_MESSAGING << clock->getName() << " iod " << clock->cached_command_name
                      << " notify_period=" << period_ms << "ms phase=" << phase_ms << "ms\n";
        clock->notifyCommandConsumers(clock->cached_command_name.c_str(), period_ms, false);
        IOComponent::noteClockSend();
    }
}

bool MachineInstance::acceptsCommandInCurrentState(const char *command_name) const {
    if (!command_name || !*command_name) {
        return false;
    }
    const Message command_msg(command_name);
    for (auto it = receives_functions.find(command_msg);
         it != receives_functions.end() && it->first == command_msg; ++it) {
        MachineCommand *cmd = it->second;
        if (cmd && (cmd->autoSwitch() || cmd->matches(const_cast<MachineInstance *>(this)))) {
            return true;
        }
    }
    return false;
}

void MachineInstance::notifyDependents(Message &msg) {
    std::set<MachineInstance *>::iterator dep_iter = depends.begin();
    while (dep_iter != depends.end()) {
        MachineInstance *dep = *dep_iter++;
        if (this == dep) {
            continue;
        }
        if (!dep->is_enabled) {
            continue;
        }
        if (!dep->is_active && !dep->io_interface) {
            continue;
        }
        DBG_M_MESSAGING << _name << " should tell " << dep->getName() << " it is "
                        << current_state.getName() << " using " << msg << "\n";

        if (dep->receives(msg, this)) {
            dep->execute(msg, this);
        }

#if 0
        Action *act = dep->executingCommand();
        if (act) {
            if (act->getStatus() == Action::Suspended) {
                act->resume();
            }
            DBG_M_MESSAGING << dep->getName() << "[" << dep->getCurrent().getName()
                    << "]" << " is executing " << *act << "\n";
        }
        //TBD execute the message on the dependant machine
        if (dep->receives(msg, this)) {
            dep->execute(msg, this);
        }
        dep->setNeedsCheck();
        //      if (dep->state_machine->token_id == ClockworkToken::LIST ) dep->notifyDependents();
#endif
    }
}

Action *MachineInstance::findMatchingCommand(const std::string &command_name) {
    // find the correct command to use based on the current state
    std::multimap<std::string, MachineCommand *>::iterator cmd_it = commands.find(command_name);
    while (cmd_it != commands.end()) {
        std::pair<std::string, MachineCommand *> curr = *cmd_it++;
        if (curr.first != command_name) {
            break;
        }
        MachineCommand *cmd = curr.second;
        DBG_M_MESSAGING << _name << " checking command " << *cmd << "\n";
        if (cmd->autoSwitch() || cmd->matches(this)) {
            return cmd->retain();
        }
    }
    DBG_M_ACTIONS << _name << " no command matching " << command_name << " found\n";
    return 0;
}

Action *MachineInstance::findReceiveHandler(Transmitter *from, const Message &m,
                                            const std::string short_name, bool response_required) {
    std::map<Message, MachineCommand *>::iterator receive_handler_i =
        receives_functions.find(Message(m.getText().c_str()));
    DBG_MESSAGING << getName() << " " << state_machine->name << " (" << current_state.getName()
                  << ")" << " receiving " << m.getText() << " short name: " << short_name << "\n";
    //    if (receive_handler_i == receives_functions.end()) {
    //      DBG_MSG << getName() << " no handler found for " << m.getText() << "\n";
    //      }
    if (receive_handler_i != receives_functions.end()) {

        while (receive_handler_i != receives_functions.end() && receive_handler_i->first == m) {

            //if (debug()) {
            DBG_MESSAGING << " found event receive handler: " << (*receive_handler_i).first << "\n"
                          << "handler: " << *((*receive_handler_i).second) << "\n";
            //}
#ifndef EC_SIMULATOR
            if (state_machine && state_machine->name == "ETHERCAT_BUS") {
                const std::string &ec_state = current_state.getName();
                if (short_name == "activate") {
                    // Also allow INIT/DISCONNECTED so STARTUP SEND activate can run. Kernel transport
                    // may still be INIT/DISCONNECTED when STARTUP SEND activate fires;
                    // allow those too so elc_cycle_activate can run.
                    const bool ok =
                        (ec_state == "CONFIG" || ec_state == "CONNECTED" ||
                         ec_state == "ACTIVE"
                         || ec_state == "INIT" || ec_state == "DISCONNECTED"
                        );
                    if (ok) {
                        std::cout << "ETHERCAT activate requested (state=" << ec_state << ")\n";
                        ProcessingThread::instance()->machine.requestActivation(true);
                    }
                    else {
                        std::cerr << "ETHERCAT activate ignored in state " << ec_state << "\n";
                    }
                }
                else if (short_name == "deactivate" &&
                         (ec_state == "ACTIVE" || ec_state == "CONNECTED" ||
                          ec_state == "CONFIG")) {
                    ProcessingThread::instance()->machine.requestDeactivation(true);
                }
                return NULL;
            }
#endif
            auto &handler = receive_handler_i->second;
            if (!handler->matches(this)) {
                DBG_M_MESSAGING << "handler does not match current state/guard "
                                << current_state.getName() << "\n";
                receive_handler_i++;
                continue;
            }
            if (response_required) {
                prepareCompletionMessage(from, short_name, m.getSeq());
            }
            return (*receive_handler_i).second->retain();
        }
        if (response_required) {
            prepareCompletionMessage(from, short_name, m.getSeq());
        }
        return nullptr;
    }
    else if (from == this) {
        if (short_name == m.getText()) {
            if (response_required) {
                prepareCompletionMessage(from, short_name, m.getSeq());
            }
            return NULL;
        } // no other alternatives
        std::map<Message, MachineCommand *>::iterator receive_handler_i =
            receives_functions.find(Message(short_name.c_str()));
        while (receive_handler_i != receives_functions.end() &&
               receive_handler_i->first == Message(short_name.c_str())) {
            if (receive_handler_i->second->matches(this)) {
                if (debug()) {
                    DBG_M_MESSAGING << " found event receive handler: "
                                    << (*receive_handler_i).first << "\n"
                                    << "handler: " << *((*receive_handler_i).second) << "\n";
                }
                if (response_required) {
                    prepareCompletionMessage(from, short_name, m.getSeq());
                }

                return (*receive_handler_i).second->retain();
            }
            ++receive_handler_i;
        }
    }
    if (response_required) {
        prepareCompletionMessage(from, short_name, m.getSeq());
    }
    return NULL;
}

/*  findHandler - find a handler for a message by looking through the transition table

    if the message not in dot-form (i.e., does not contain a dot), we search the transition
    table for a matching transition

    - find a matching transition
    - if the transition is to a stable state
    - clear the trigger on the condition handlers for the state
    - construct a stable state IF test and push it to the action stack
    - if a response is required push an ExecuteMessage action  for the _done message
    - otherwise,
    -push a move state action
    - if there is a matching command (unnecessary test) and the command is exported turn the command coil off

    If the message is in dot-form (i.e., includes a machine name), we treat is as a simple
    message :
    - do not execute any command linked to the transition
    - do not check requirements
    - push a move state action

*/

Action::Status MachineInstance::invokeReceive(const Message &msg, Transmitter *from,
                                              bool needs_receipt, Action **out_handler) {
    if (out_handler) {
        *out_handler = nullptr;
    }
    Message m(msg);
    if (io_interface && from == io_interface) {
        const Action::Status sent = execute(m, from);
        return (sent == Action::Failed) ? Action::Failed : Action::Complete;
    }
    Action *h = findHandler(m, from, needs_receipt);
    if (!h) {
        return Action::Complete;
    }
    const Action::Status stat = (*h)();
    if (out_handler) {
        *out_handler = h;
    }
    else {
        h->release();
    }
    return stat;
}

Action *MachineInstance::findHandler(Message &m, Transmitter *from, bool response_required) {
    std::string short_name(m.getText());
    if (short_name.find('.') != std::string::npos) {
        short_name = short_name.substr(short_name.find('.') + 1);
    }
    if (from) {
        DBG_M_MESSAGING << _name << " received message " << m << " from " << from->getName()
                        << "\n";
    }
    if (from == this || m.getText() == short_name) {
        DBG_M_MESSAGING << _name << " checking transitions (" << transitions.size() << ")\n";
        std::list<Transition>::const_iterator iter = transitions.begin();
        while (iter != transitions.end()) {
            const Transition &t = *iter++;
            if (t.trigger.getText() == short_name &&
                (current_state == t.source || t.source == "ANY") &&
                (!t.condition || t.condition->operator()(this))) {
                // found match, if there is a command defined for this transition, we
                // execute the command, otherwise we just do the state change
                bool state_change_ok = true;
                auto num_commands = commands.count(t.trigger.getText());
                if (num_commands) {
                    DBG_M_MESSAGING << "Transition on machine " << getName()
                                    << " has a linked command; using it\n";
                    // at the end of the transition, we expect to have moved to the designated state
                    // so we ensure that happens by pushing a state change
                    // but first, we check the stablestate condition for that state.

                    // find state condition
                    bool found = false;
                    IfElseCommandAction *change_state_action = 0;
                    for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
                        StableState &s = stable_states[ss_idx];
                        if (s.state_name == t.dest.getName()) {
                            DBG_M_STATECHANGES << "found stable state condition test for "
                                               << t.dest.getName() << "\n";
                            if (s.subcondition_handlers) {
                                std::list<ConditionHandler>::iterator iter =
                                    s.subcondition_handlers->begin();
                                while (iter != s.subcondition_handlers->end()) {
                                    ConditionHandler &ch = *iter++;
                                    ch.triggered = false;
                                }
                            }
                            if (change_state_action == 0) {
                                MoveStateActionTemplate temp(_name.c_str(),
                                                             t.dest.getName().c_str());
                                MachineCommandTemplate mc("stable_state_test", "");
                                mc.setActionTemplate(&temp);
                                char buf[100];
                                snprintf(buf, 100, "%s: Failed Transition from %s to %s",
                                         _name.c_str(), current_state.getName().c_str(),
                                         t.dest.getName().c_str());
                                AbortActionTemplate aat(true, Value(buf, Value::t_string));
                                MachineCommandTemplate mc2("abort", "");
                                mc2.setActionTemplate(&aat);
                                IfElseCommandActionTemplate ifecat(s.condition.predicate, &mc,
                                                                   &mc2);
                                change_state_action = new IfElseCommandAction(this, &ifecat);
                            }
                            else {
                                // update the action to or with another condition
                                Predicate *p =
                                    new Predicate(change_state_action->condition.predicate, opOR,
                                                  new Predicate(*s.condition.predicate));
                                change_state_action->condition.predicate = p;
                            }
                            found = true;
                        }
                    }
                    if (change_state_action) {
                        this->enqueueAction(change_state_action);
                    }
                    // the CALL method waits for a response once the executed command is complete
                    // the response will be sent after the transition to the next state is done
                    if (response_required) {
                        prepareCompletionMessage(from, t.trigger.getText(), m.getSeq());
                    }

                    if (!found) {
                        DBG_M_STATECHANGES << "no stable state condition test for "
                                           << t.dest.getName() << " pushing state change\n";
                        MoveStateActionTemplate msat("SELF", t.dest.getName());
                        MoveStateAction *msa = new MoveStateAction(this, msat);
                        DBG_M_STATECHANGES << _name << " pushed (status: " << msa->getStatus()
                                           << ") " << *msa << "\n";
                        this->push(msa);
                    }
                    // if a modbus command, make sure it is turned off again in modbus
                    if (published && modbus_exports.count(_name + "." + t.trigger.getText())) {
                        ModbusAddress addr = modbus_exports[_name + "." + t.trigger.getText()];
                        if (addr.getSource() == ModbusAddress::command) {
                            DBG_MODBUS << _name << " turning off command coil " << addr << "\n";
                            addr.update(this, 0);
                        }
                        else {
                            NB_MSG << _name << " command " << t.trigger.getText()
                                   << " is linked to an improper address\n";
                        }
                    }
                    // find the correct command to use based on the current state
                    Action *matching_command = findMatchingCommand(t.trigger.getText());
                    if (matching_command) {
                        return matching_command;
                    }
                }
                else {

#ifndef EC_SIMULATOR
                    if (state_machine->name == "ETHERCAT_BUS") {
                        if (short_name == "activate" && (current_state.getName() == "CONFIG" ||
                                                         current_state.getName() == "CONNECTED")) {
                            ProcessingThread::instance()->machine.requestActivation(true);
                            ProcessingThread::activate(this);
                        }
                        else if (short_name == "deactivate" &&
                                 current_state.getName() == "ACTIVE") {
                            ProcessingThread::instance()->machine.requestDeactivation(true);
                            ProcessingThread::activate(this);
                        }
                    }
                    else
#endif
                        DBG_M_MESSAGING
                            << "No linked command for the transition, performing state change\n";
                }
                if (response_required) {
                    prepareCompletionMessage(from, t.trigger.getText(), m.getSeq());
                }

                if (state_change_ok) {
                    // no matching command, just perform the transition
                    MoveStateActionTemplate temp(_name.c_str(), t.dest.getName().c_str());
                    return new MoveStateAction(this, temp);
                }
                else {
                    SendMessageActionTemplate ssat("StateChangeFailureException", this);
                    return new SendMessageAction(this, ssat);
                }
            }
        }
        // no transition but this may still be a command
        DBG_M_MESSAGING << _name << " looking for a command with name " << short_name << "\n";
        if (commands.count(short_name)) {
            Action *matching_command = findMatchingCommand(short_name);
            if (response_required) {
                prepareCompletionMessage(from, short_name, m.getSeq());
            }
            if (matching_command) {
                return matching_command;
            }
            else {
                DBG_M_MESSAGING << _name << "found a command but it doesn't match state "
                                << current_state.getName() << "\n";
            }
        }
    }
    else {
        std::list<Transition>::const_iterator iter = transitions.begin();
        while (iter != transitions.end()) {
            const Transition &t = *iter++;
            if ((t.trigger.getText() == m.getText() || t.trigger.getText() == short_name) &&
                current_state == t.source) {
                if (response_required) {
                    prepareCompletionMessage(from, short_name, m.getSeq());
                }
                DBG_M_MESSAGING << _name << " received message" << m.getText()
                                << "; pushing state change\n";
                MoveStateActionTemplate msat("SELF", t.dest.getName());
                MoveStateAction *msa = new MoveStateAction(this, msat);
                DBG_M_STATECHANGES << _name << " pushed (status: " << msa->getStatus() << ") "
                                   << *msa << "\n";
                return msa;
            }
        }
    }
    // if debugging, generate a log message for the enter function
    if (debug() && _type != "POINT" &&
        LogState::instance()->includes(DebugExtra::instance()->DEBUG_ACTIONS)) {
        std::string message_str = _name + "." + current_state.getName() + "_enter";
        if (message_str != m.getText()) {
            DBG_M_MESSAGING << _name << ": No transition found to handle " << m << " from "
                            << current_state.getName()
                            << ". Searching receive_functions for a handler...\n";
        }
        else {
            DBG_M_MESSAGING << _name << ": Looking for ENTER method for " << current_state.getName()
                            << "\n";
        }
    }
    return findReceiveHandler(from, m, short_name, response_required);
}

void MachineInstance::prepareCompletionMessage(Transmitter *from, std::string message,
                                               unsigned long correlation_id) {
    if (from) {
        MachineInstance *from_mi = dynamic_cast<MachineInstance *>(from);
        assert(from_mi);
        std::string response = _name + "." + message + "_done";
        // Echo the caller's message sequence so a CALL can match only the reply
        // to its own command (not a late reply from an earlier CALL).
        if (correlation_id != 0) {
            response += ".";
            response += std::to_string(correlation_id);
        }
        //DBG_MSG << _name << " command " << message << " completion requires response. Adding command to execute "
        //<< response << " on: " << from_mi->fullName()
        //<< ( (from_mi->enabled()) ? " (enabled)\n" : " (disabled)\n");
        ExecuteMessageActionTemplate emat(strdup(response.c_str()), from_mi);
        ExecuteMessageAction *ema = new ExecuteMessageAction(from_mi, emat);
        this->push(ema);
    }
    else {
        std::stringstream ss;
        ss << fullName() << ": command " << message
           << " completion requires response but the sender is not set";
        char buf[300];
        snprintf(buf, 300, "%s", ss.str().c_str());
        MessageLog::instance()->add(buf);
        DBG_MSG << buf << "\n";
    }
}

Action::Status MachineInstance::execute(const Message &m, Transmitter *from, Action *action) {
    if (!enabled()) {
        std::string msg(m.getText());
        size_t len = msg.length();
        if (len > 5 && msg.substr(len - 5) == "_done") {
            char buf[120];
            if (from) {
                snprintf(buf, 120, "%s failed to execute %s from %s while disabled", _name.c_str(),
                         m.getText().c_str(), from->getName().c_str());
            }
            else {
                snprintf(buf, 120, "%s failed to execute %s while disabled", _name.c_str(),
                         m.getText().c_str());
            }
            MessageLog::instance()->add(buf);
        }
        if (action) {
            action->setError("failed to receive a message while disabled");
        }
        return Action::Failed;
    }

    if (m.getText().length() == 0) {
        NB_MSG << _name << " error: dropping empty message\n";
        return Action::Failed;
    }

    setNeedsCheck();

    std::string event_name(m.getText());
    if (from && event_name.find('.') == std::string::npos) {
        event_name = from->getName() + "." + m.getText();
    }

    // fire the triggers on any suspended commands that might be waiting for them.
    if (executingCommand()) {
        // tell any actions that are triggering on this that we have seen the event
        DBG_M_MESSAGING << _name << " processing triggers for " << event_name << "\n";
        std::list<Action *>::iterator iter = active_actions.begin();
        while (iter != active_actions.end()) {
            Action *a = *iter++;
            Trigger *trigger = a->getTrigger();
            if (trigger && trigger->matches(m.getText())) {
                SetStateAction *msa = dynamic_cast<MoveStateAction *>(a);
                SetStateAction *ssa = dynamic_cast<SetStateAction *>(a);
                CallMethodAction *cma = dynamic_cast<CallMethodAction *>(a);
                if (ssa) {
                    if (!ssa->getCondition().predicate || ssa->getCondition()(this)) {
                        trigger->fire();
                        DBG_M_MESSAGING << _name << " trigger on " << *a << " fired\n";
                    }
                    else {
                        DBG_M_MESSAGING << _name << " trigger message " << trigger->getName()
                                        << " arrived but condition "
                                        << *(ssa->getCondition().predicate) << " failed\n";
                        char buf[150];
                        snprintf(
                            buf, 150,
                            "%s dropped set state trigger without firing since the state condition on %s is no longer valid",
                            fullName().c_str(), event_name.c_str());
                        MessageLog::instance()->add(buf);
                        DBG_MSG << buf << "\n";
                    }
                }
                else if (msa) {
                    if (!msa->getCondition().predicate || msa->getCondition()(this)) {
                        trigger->fire();
                        DBG_M_MESSAGING << _name << " trigger on " << *a << " fired\n";
                    }
                    else {
                        DBG_M_MESSAGING << _name << " trigger message " << trigger->getName()
                                        << " arrived but condition "
                                        << *(ssa->getCondition().predicate) << " failed\n";
                        char buf[150];
                        snprintf(
                            buf, 150,
                            "%s dropped move state trigger without firing since the state condition on %s is no longer valid",
                            fullName().c_str(), event_name.c_str());
                        MessageLog::instance()->add(buf);
                        DBG_MSG << buf << "\n";
                    }
                }
                // a call method action may be waiting for this message
                else if (cma) {
                    if (cma->getTrigger()) {
                        cma->getTrigger()->fire();
                    }
                    DBG_M_MESSAGING << _name << " trigger on " << *a << " fired\n";
                }
                else {
                    NB_MSG << _name << " firing unknown trigger on " << *a << "\n";
                    trigger->fire();
                }
            }
        }
    }
    DBG_M_MESSAGING << _name << " executing message " << m.getText() << " from "
                    << ((from) ? from->getName() : "unknown") << "\n";

    if ((io_interface && from == io_interface) || (mq_interface && from == mq_interface)) {
        // for machines that are connected to hardware on/off transitions occur when the
        // machine receives an on/off_enter message from the hardware interface
        // Similarly, properties are updated in response to 'property_change' message from
        // the hardware interface

        if (!state_machine) {
            return Action::Failed;
        }

        if (m.getText() == "on_enter" && current_state.getName() != "on") {
            const State *on = state_machine->findState("on");
            if (on) {
                setState(*on, expected_authority, false);
            }
            else {
                char buf[120];
                snprintf(buf, 120, "%s does not have a state 'on' executing 'on_enter'",
                         _name.c_str());
                MessageLog::instance()->add(buf);
                return Action::Failed;
            }
        }
        else if (m.getText() == "off_enter" && current_state.getName() != "off") {
            const State *off = state_machine->findState("off");
            if (off) {
                setState(*off, expected_authority, false);
            }
            else {
                char buf[120];
                snprintf(buf, 120, "%s does not have a state 'off' executing 'off_enter'",
                         _name.c_str());
                MessageLog::instance()->add(buf);
                return Action::Failed;
            }
        }
        else if (m.getText() == "property_change") {
            //std::cout << _name << " value changed to " << io_interface->address.value << "\n";
            setValue("VALUE", io_interface->address.value);
        }
        // a POINT won't have an actions that depend on triggers so it's safe to return now
        return Action::Complete;
    }
    // execute the method if there is one
    DBG_M_MESSAGING << _name << " executing " << event_name << "\n";
    Message msg(event_name.c_str());
    HandleMessageActionTemplate hmat(Package(from, this, msg));
    HandleMessageAction *hma = new HandleMessageAction(this, hmat);
    Action::Status res = (*hma)();
    hma->release();
    return res;
}

void MachineInstance::handle(const Message &m, Transmitter *from, bool send_receipt) {
    if (!enabled()) {
        DBG_M_MESSAGING << _name << ":" << id << " dropped: " << m << " in " << getCurrent()
                        << " from " << from->getName() << " while disabled\n";
        if (send_receipt) {
            NB_MSG << _name << " Error: message '" << m.getText()
                   << "' requiring a receipt arrived while disabled\n";
        }
        return;
    }
    if (_type != "POINT") {
        DBG_M_MESSAGING << _name << ":" << id << " received: " << m << " in " << getCurrent()
                        << " from " << from->getName() << " will send receipt: " << send_receipt
                        << "\n";
    }
    HandleMessageActionTemplate hmat(Package(from, this, m, send_receipt));
    HandleMessageAction *hma = new HandleMessageAction(this, hmat);
    enqueueAction(hma);
    SharedWorkSet::instance()->add(this);
    ProcessingThread::activate(this);
}

void MachineInstance::sendMessageToReceiver(const Message &message, Receiver *r,
                                            bool expect_reply) {
    MachineShadowInstance *msi = dynamic_cast<MachineShadowInstance *>(r);
    if (msi) {
        DBG_CHANNELS << _name << " sending message " << message << " to shadow " << r->getName()
                     << "\n";
        std::string addressed_message = r->getName();
        addressed_message += ".";
        addressed_message += message.getText();
        std::list<Value> *params = new std::list<Value>;
        params->push_back(addressed_message.c_str());
        MachineInstance *mi = dynamic_cast<MachineInstance *>(r);
        if (mi) {
            MessageHeader mh(MessageHeader::SOCK_CW, MessageHeader::SOCK_CW, false);
            mh.start_time = microsecs();
            Channel::sendCommand(mi, "SEND", params, mh); // empty command parameter list
        }
        else {
            char buf[150];
            snprintf(buf, 150, "Channel is unable to send to non machine instance");
            MessageLog::instance()->add(buf);
            DBG_MSG << buf << "\n";
        }
    }
    else {
        DBG_MESSAGING << _name << " sending message " << message << " to " << r->getName() << "\n";
        if (r->enabled() ||
            expect_reply) { // allow the call to hang here, this will change when throw works TBD
            DBG_M_MESSAGING << _name << " message " << message.getText()
                            << " expect reply: " << expect_reply << "\n";
            MachineInstance *dest = dynamic_cast<MachineInstance *>(r);
            Channel *chn = dynamic_cast<Channel *>(r);
            if (dest && !chn) {
                dest->enqueue(Package(this, dest, message, expect_reply));
                SharedWorkSet::instance()->add(dest);
                ProcessingThread::activate(dest);
            }
            else {
                Package *p = new Package(this, r, message, expect_reply);
                Dispatcher::instance()->deliver(p);
            }
        }
        else {
#if 0
            char buf[120];
            snprintf(buf, 120, "%s: sending %s to %s failed because the target is not enabled",
                    _name.c_str(), m->getText().c_str(), r->getName().c_str());
            MessageLog::instance()->add(buf);
            DBG_MSG << buf << "\n";
#endif
            Message msg("DisabledMessageTargetException");
            Package *p = new Package(this, this, msg, false);
            Dispatcher::instance()->deliver(p);
        }
    }
}

void MachineInstance::sendMessageToReceiver(const char *msg, Receiver *r, bool expect_reply) {
    Message m(msg);
    sendMessageToReceiver(m, r, expect_reply);
}

MachineEvent::MachineEvent(MachineInstance *machine, Message *message)
    : mi(machine), msg(message) {}

MachineEvent::MachineEvent(MachineInstance *machine, const Message &message)
    : mi(machine), msg(new Message(message)) {}

MachineEvent::~MachineEvent() { delete msg; }

Action *MachineInstance::executingCommand() {
    if (!active_actions.empty()) {
        return active_actions.back();
    }
    return 0;
}

void MachineInstance::start(Action *a) {
    if (a->started()) {
        return;
    }
    a->start();
    if (tracing() && isTraceable()) {
        resetTemporaryStringStream();
        ss << "starting action: " << *a;
        setValue("TRACE", Value(ss.str(), Value::t_string));
    }
    DBG_M_ACTIONS << _name << " STARTING: " << *a << "\n";

    int i = 0;
    // actions execute from the back of the queue
    // an action may have been pushed and then started or just started
    // if it is not the item at the back of the queue we push it now
    Action *curr = executingCommand();
    if (a != curr) {
        active_actions.push_back(a->retain());
        ProcessingThread::activate(this);
    }
    else if (curr) {
        setNeedsCheck();
    }
}

void MachineInstance::displayActive(std::ostream &note) {
    if (active_actions.empty()) {
        return;
    }
    note << _name << ':' << id << " stack:\n";
    const char *delim = "";
    std::list<Action *>::iterator iter = active_actions.begin();
    while (iter != active_actions.end()) {
        Action *act = *iter++;
        note << delim << " " << *act << " status: " << act->getStatus();
        const char *error_msg = act->error();
        if (error_msg && *error_msg) {
            note << " error: " << error_msg;
        }
        delim = "\n";
    }
}

// stop removes the action
void MachineInstance::stop(Action *a) {
    //  if (active_actions.back() != a) {
    //      DBG_M_ACTIONS << _name << "Top of action stack is no longer " << *a << "\n";
    //      return;
    //  }
    //  active_actions.pop_back();
    //    a->release();
    if (!a->started()) {
        //      DBG_MSG << _name << " warning: action " << *a << " was stopped but not yet started\n";
    }
    a->setTrigger(0);
    a->stop();
    bool found = false;
    std::list<Action *>::iterator iter = active_actions.begin();
    while (iter != active_actions.end()) {
        Action *queued = *iter;
        if (queued == a) {
            if (found) {
                std::cerr << "Warning: action " << *a << " was queued twice\n";
            }
            iter = active_actions.erase(iter);
            DBG_M_ACTIONS << fullName() << " removed action " << *a << "\n";
            a->release();
            found = true;
            continue;
        }
        iter++;
    }

    if (!active_actions.empty()) {
        Action *next = active_actions.back();
        if (next) {
            next->setBlocker(0);
            if (next->suspended()) {
                next->resume();
            }
        }
    }
    setNeedsCheck();
}

void MachineInstance::clearAllActions() {
    while (!active_actions.empty()) {
        Action *a = active_actions.front();
        active_actions.pop_front();

        DBG_M_ACTIONS << "Releasing action " << *a << "\n";
        // note that we can't delete an action with active triggers, instead we disable the trigger
        // and wait for the scheduler to delete the action
        if (a->getTrigger() && a->getTrigger()->enabled() && !a->getTrigger()->fired()) {
            a->disableTrigger();
        }
        a->release();
    }

    boost::mutex::scoped_lock lock(q_mutex);
    mail_queue.clear();
    has_work = false;
}

void MachineInstance::markActive() {
    is_active = true;
    active_machines.remove(this);
    active_machines.push_back(this);
    if (state_machine && state_machine->plugin) {
        markPlugin();
    }
}

#if 0
void MachineInstance::markPassive()
{
    is_active = false;
    active_machines.remove(this);
}
#endif

void MachineInstance::markPlugin() { plugin_machines.insert(this); }

void MachineInstance::resume() {
    if (!is_enabled) {

        // fix status
        is_enabled = true;
        error_state = 0;
        for (unsigned int i = 0; i < locals.size(); ++i) {
            locals[i].machine->resume();
        }
        setState(current_state, expected_authority, true);
        // adjust the starttime of the current state to make allowance for the
        // time we were disabled
        MachineTimerValue *mtv = dynamic_cast<MachineTimerValue *>(state_timer.dynamicValue());
        if (mtv) {
            mtv->resume();
        }
        setNeedsCheck();
    }
}

void fixListState(MachineInstance &list) {
    const State *s = 0;
    if (list.parameters.empty()) {
        s = list.getStateMachine()->findState("empty");
    }
    else {
        s = list.getStateMachine()->findState("nonempty");
    }
    assert(s);
    list.setState(*s);
}

void MachineInstance::setInitialState(bool resume) {
    if (io_interface) {
        io_interface->setInitialState();
    }
    if (state_machine) {
        if (io_interface) {
            const State *s = state_machine->findState(io_interface->getStateString());
            if (s) {
                setState(*s, expected_authority, false);
            }
            else {
                char buf[100];
                snprintf(buf, 100, "Warning: setting initial state on %s to unknown state: %s\n",
                         _name.c_str(), io_interface->getStateString());
                MessageLog::instance()->add(buf);
                NB_MSG << buf << "\n";
            }
        }
        else {
            if (state_machine->token_id == ClockworkToken::LIST) {
                fixListState(*this);
            }
            else {
                setState(state_machine->initial_state, expected_authority, resume);
            }
            setNeedsCheck();
        }
    }
    else {
        char buf[100];
        snprintf(buf, 100, "Warning: setting initial state on %s when it has no state machine\n",
                 _name.c_str());
        MessageLog::instance()->add(buf);
        NB_MSG << buf << "\n";
    }
}

void MachineInstance::publish() { ++published; }

// Prior to enabling or disabling a machine, use sortDependentMachines to try
// to enable local machines in the dependency order.
void sortDependentMachines(MachineInstance *m, std::list<MachineInstance *> &sorted) {
    // enable related machines first (locals and (iff this is a list) parameters
    std::set<MachineInstance *> related_machines;
    for (unsigned int i = 0; i < m->locals.size(); ++i) {
        if (m->locals[i].machine) {
            related_machines.insert(m->locals[i].machine);
        }
    }

    // lists that are instantiated globally provide the feature of enabling or disabling all members
    // when they are enabled or disabled. This is not true for lists instantiated within another
    // statemachine
    if (m->_type == "LIST" && !m->owner) {
        for (unsigned int i = 0; i < m->parameters.size(); ++i) {
            if (m->parameters[i].machine) {
                related_machines.insert(m->parameters[i].machine);
            }
        }
    }
    if (!related_machines.empty()) {
        std::set<MachineInstance *>::iterator iter = related_machines.begin();
        while (iter != related_machines.end()) {
            MachineInstance *a = *iter;
            std::list<MachineInstance *>::iterator oi = sorted.begin();
            while (oi != sorted.end()) {
                MachineInstance *b = *oi;
                if (!a->uses(b)) {
                    break;
                }
                oi++;
            }
            if (oi == sorted.end()) {
                sorted.push_back(a);
            }
            else {
                sorted.insert(oi, a);
            }
            iter++;
        }
    }
}

void MachineInstance::unpublish() { --published; }

void MachineInstance::enable() {
    // Enabling a machine that is already enabled should wake it up.
    if (is_enabled && _type != "LIST" && _type != "REFERENCE") {
        setNeedsCheck();
        return;
    }
    is_enabled = true;
    error_state = 0;
    clearAllActions();
    if (io_interface) {
        io_interface->setupProperties(this);
        io_interface->handleChange(pending_events);
    }
    std::list<MachineInstance *> sorted;
    sortDependentMachines(this, sorted);
    std::list<MachineInstance *>::iterator oi = sorted.begin();
    while (oi != sorted.end()) {
        MachineInstance *b = *oi++;
        b->enable();
    }

    if (isActive() && !isShadow() && getStateMachine()->token_id != ClockworkToken::EXTERNAL) {
        std::string msgstr(_name);
        msgstr += "_enabled";
        Message msg(msgstr.c_str(), Message::ENABLEMSG);
        sendMessageToReceiver(msg, this, false);
    }

    if (_type == "LIST") {
        fixListState(*this);
    }
    else if (_type == "REFERENCE") {
        if (locals.size()) {
            const State *s = state_machine->findState("ASSIGNED");
            setState(*s);
        }
        else {
            const State *s = state_machine->findState("EMPTY");
            setState(*s);
        }
    }
    else {
        setInitialState(true);
    }

    setNeedsCheck();
    // if any dependent machines are already enabled, make sure they know we are awake
    std::set<MachineInstance *>::iterator d_iter = depends.begin();
    while (d_iter != depends.end()) {
        MachineInstance *dep = *d_iter++;
        if (dep->enabled()) {
            dep->setNeedsCheck();
            // Quiet LISTs suppress ordinary cascade; enable of a member must
            // still wake LIST dependents (LISTALLENABLED, etc.).
            if (dep->getStateMachine() &&
                dep->getStateMachine()->token_id == ClockworkToken::LIST &&
                !dep->listPropagatesMemberChecks()) {
                dep->propagateNeedsCheckToDependents();
            }
        }
    }
}

void MachineInstance::setDebug(bool which) {
    allow_debug = which;
    for (unsigned int i = 0; i < locals.size(); ++i) {
        locals[i].machine->setDebug(which);
    }
}

void MachineInstance::disable() {
    if (!is_enabled && _type != "LIST" && _type != "REFERENCE") {
        setNeedsCheck();
        return;
    }
    getTimerVal(); // update the timer in case a resume occurs
    if (locked) {
        locked = 0;
    }
    if (io_interface) {
        io_interface->turnOff();
    }
    if (isShadow()) {
        setInitialState();
    }
    // Drop in-flight SetState/mail before marking disabled. enable()/resume()
    // already clear; without this, actions queued just before DISABLE stay
    // New forever (idle() refuses to run them while disabled) and keep the
    // machine on the runnable/SharedWorkSet spin path.
    clearAllActions();
    SharedWorkSet::instance()->remove(this);
    ProcessingThread::suspend(this); // no-op if PT not initialised yet
    resetNeedsCheck();
    is_enabled = false;

    const Value &val = properties.lookup("default");
    if (val != SymbolTable::Null) {
        if (val.kind == Value::t_integer) {
            int64_t i_val = 0;
            if (val.asInteger(i_val)) {
                setValue("VALUE", i_val);
                if (io_interface) {
                    if (io_interface->address.is_signed) {
                        io_interface->setValue((int32_t)(i_val & 0xffffffff));
                    }
                    else {
                        io_interface->setValue((uint32_t)(i_val & 0xffffffff));
                    }
                }
            }
        }
    }

    disabled_time = microsecs();
    std::list<MachineInstance *> sorted;
    sortDependentMachines(this, sorted);
    std::list<MachineInstance *>::reverse_iterator oi = sorted.rbegin();
    while (oi != sorted.rend()) {
        MachineInstance *b = *oi++;
        b->disable();
    }
    // if any dependent machines are already enabled, make sure they know we are disabled
    std::set<MachineInstance *>::iterator d_iter = depends.begin();
    while (d_iter != depends.end()) {
        MachineInstance *dep = *d_iter++;
        if (dep->enabled()) {
            dep->setNeedsCheck();
            if (dep->getStateMachine() &&
                dep->getStateMachine()->token_id == ClockworkToken::LIST &&
                !dep->listPropagatesMemberChecks()) {
                dep->propagateNeedsCheckToDependents();
            }
        }
    }
}

void MachineInstance::resume(const State &s) {
    if (!is_enabled) {
        // fix status
        clearAllActions();
        is_enabled = true;
        error_state = 0;
        // resume all local machines first so that setState() can
        // execute any necessary methods on local machines
        for (unsigned int i = 0; i < locals.size(); ++i) {
            locals[i].machine->resume();
        }
        setState(s, expected_authority, true);
        setNeedsCheck();
        if (_type == "LIST") // resuming a list resumes the members
            for (unsigned int i = 0; i < parameters.size(); ++i) {
                // parameters my be just values but if they are machines they need to be enabled
                if (parameters[i].machine) {
                    parameters[i].machine->resume();
                }
            }
        std::set<MachineInstance *>::iterator d_iter = depends.begin();
        while (d_iter != depends.end()) {
            MachineInstance *dep = *d_iter++;
            if (dep->enabled()) {
                dep->setNeedsCheck();
            }
        }
    }
}

void MachineInstance::resumeAll() {
    // resume all local machines that have not already been enabled
    for (unsigned int i = 0; i < locals.size(); ++i) {
        if (!locals[i].machine->enabled()) {
            locals[i].machine->resume();
        }
    }
}

void MachineInstance::push(Action *new_action) {
    if (!active_actions.empty()) {
        active_actions.back()->suspend();
        active_actions.back()->setBlocker(new_action);
    }
    DBG_M_ACTIONS << _name << " pushing " << *new_action << "\n";
    active_actions.push_back(new_action);
    new_action->start();
    num_machines_with_work++;
    has_work = true;
    SharedWorkSet::instance()->add(this);
    ProcessingThread::activate(this);
    if (tracing() && isTraceable()) {
        resetTemporaryStringStream();
        ss << "starting action: " << *new_action;
        setValue("TRACE", Value(ss.str(), Value::t_string));
    }
    DBG_M_ACTIONS << _name << " ADDED to machines with work " << SharedWorkSet::instance()->size()
                  << "\n";
    return;
}

void MachineInstance::updateLastEvaluationTime() {
    last_state_evaluation_time = microsecs();
    if (idle_time) {
        next_poll = last_state_evaluation_time + idle_time;
    }
    else {
        if (state_machine && state_machine->polling_delay) {
            next_poll = last_state_evaluation_time + state_machine->polling_delay;
        }
        else if (MachineInstance::polling_delay) {
            next_poll = last_state_evaluation_time + MachineInstance::polling_delay->iValue;
        }
    }
}

bool MachineInstance::isStableState(const std::string state_name) {
    for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
        StableState &s = stable_states[ss_idx];
        if (s.state_name == state_name) {
            return true;
        }
    }
    return false;
}

std::string MachineInstance::firstValidStableState(const std::string state_name) {
    std::string found;
    for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
        StableState &s = stable_states[ss_idx];
        if (s.condition(this)) {
            found = s.state_name;
            break;
        }
    }
    // TODO: Investigate whether the default state can be empty and how that affects things
    return found.empty() ? this->state_machine->default_state.getName() : found;
}

bool MachineInstance::setStableState() {
    bool changed_state = false;
    ProcessingThread::suspend(
        this); // assume this machine will not have anything else to do after checking states

    CaptureDuration cd(stable_states_stats);
    DBG_M_AUTOSTATES << _name << " checking stable states (currently " << current_state.getName()
                     << ")\n";
    if (!state_machine || !state_machine->allow_auto_states) {
        //DBG_M_AUTOSTATES << _name << " aborting stable states check due to configuration\n";
        DBG_MSG << _name << " aborting stable states check due to configuration\n";
        return false;
    }
    if (executingCommand() || !mail_queue.empty()) {
        //DBG_M_AUTOSTATES << _name << " aborting stable states check due to command execution\n";
        DBG_MSG << _name << " aborting stable states check due to command execution\n";
        return false;
    }
    // we must not set our stable state if objects we depend on are still updating their own state
    needs_check = 0;
    updateLastEvaluationTime();

    if (io_interface) {
        const char *io_state_name = io_interface->getStateString();
        const State *s = state_machine->findState(io_state_name);
        if (s) {
            setState(*s);
        }
        else {
            char buf[200];
            snprintf(buf, 200, "Warning: %s does not have a state %s to match the hardware\n",
                     _name.c_str(), io_state_name);
            MessageLog::instance()->add(buf);
            NB_MSG << buf << "\n";
        }
    }
    else if ((!state_machine && _type == "LIST") ||
             (state_machine && state_machine->token_id == ClockworkToken::LIST)) {
        //DBG_MSG << _name << " has " << parameters.size() << " parameters\n";
        fixListState(*this);
    }
    else if ((!state_machine && _type == "REFERENCE") ||
             (state_machine && state_machine->token_id == ClockworkToken::REFERENCE)) {
        //DBG_MSG << _name << " has " << parameters.size() << " parameters\n";
        if (locals.size()) {
            const State *s = state_machine->findState("ASSIGNED");
            assert(s);
            setState(*s);
        }
        else {
            const State *s = state_machine->findState("EMPTY");
            assert(s);
            setState(*s);
        }
    }
    else {
        bool found_match = false;
        const long MAX_TIMER = 100000000L;
        PredicateTimerDetails *ptd = 0;
        StableState *active_state = 0;
        for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
            StableState &s = stable_states[ss_idx];
            // the following test should be enabled but there is currently a situation that
            // can cause the stable state to not be evaluated when a trigger is set. TBD
            //      if ( (s.trigger && s.trigger->enabled() && s.trigger->fired() && s.condition(this) )
            //          || (!s.trigger && s.condition(this)) ) {
            if (!found_match) {
                bool ss_condition_true = s.condition(this);
                if (ss_condition_true) {
                    DBG_M_PREDICATES << _name << "." << s.state_name << " condition "
                                     << *s.condition.predicate << " returned true\n";
                    active_state = &s;
                    if (current_state.getName() != s.state_name) {
                        DBG_M_AUTOSTATES << " changing state\n";
                        changed_state = true;
                        if (tracing() && isTraceable()) {
                            resetTemporaryStringStream();
                            ss << current_state.getName() << "->" << s.state_name << " "
                               << *s.condition.predicate;
                            setValue("TRACE", Value(ss.str(), Value::t_string));
                        }
                        if (s.subcondition_handlers) {
                            std::list<ConditionHandler>::iterator iter =
                                s.subcondition_handlers->begin();
                            while (iter != s.subcondition_handlers->end()) {
                                ConditionHandler &ch = *iter++;
                                ch.triggered = false;
                            }
                        }
                        DBG_AUTOSTATES << _name << ":" << id << " (" << current_state
                                       << ") should be in state " << s.state_name
                                       << " due to condition: " << *s.condition.predicate << "\n";
                        char *sn = strdup(s.state_name.c_str());
                        SetStateActionTemplate ssat(CStringHolder("SELF"), s.state_name,
                                                    StateChangeReason::automatic);
                        enqueueAction(ssat.factory(
                            this)); // execute this state change next time actions are processed
                        free(sn);
                    }
                    else {
                        DBG_AUTOSTATES << _name << " is already in " << s.state_name
                                       << " checking subconditions\n";
                        if (s.subcondition_handlers) {
                            std::list<ConditionHandler>::iterator iter =
                                s.subcondition_handlers->begin();
                            while (iter != s.subcondition_handlers->end()) {
                                ConditionHandler *ch = &(*iter++);
                                DBG_AUTOSTATES
                                    << "checking subcondition: " << (*ch).condition.last_evaluation
                                    << "\n";
                                if (tracing() && isTraceable()) {
                                    resetTemporaryStringStream();
                                    ss << current_state.getName() << "->" << s.state_name << " "
                                       << *ch->condition.predicate;
                                    setValue("TRACE", Value(ss.str(), Value::t_string));
                                }
                                if (!ch->check(this)) {
                                    ptd = ch->condition.predicate->scheduleTimerEvents(ptd, this);
                                }
                            }
                        }
                        if (s.uses_timer) {
                            DBG_SCHEDULER << _name << "[" << current_state.getName()
                                          << "] checking condition tests for rule #" << ss_idx
                                          << " state: " << s.state_name << "\n";
                            ptd = s.condition.predicate->scheduleTimerEvents(ptd, this);
                            if (ptd) {
                                DBG_M_SCHEDULER << "found timer event " << ptd->label
                                                << " t: " << ptd->delay << " on rule #" << ss_idx
                                                << " state: " << s.state_name << "\n";
                            }
                        }
                    }
                    found_match = true;
                    break; // skip to the end of the stable state loop
                }
                else {
                    DBG_PREDICATES << _name << " " << s.state_name << " condition "
                                   << *s.condition.predicate << " returned false\n";
                    if (s.uses_timer) {
                        DBG_SCHEDULER << _name << "[" << current_state.getName()
                                      << "] scheduling condition tests for state " << s.state_name
                                      << "\n";
                        ptd = s.condition.predicate->scheduleTimerEvents(ptd, this);
                        if (ptd) {
                            DBG_M_SCHEDULER << "found timer event " << ptd->label
                                            << " t: " << ptd->delay << " on rule #" << ss_idx
                                            << " state: " << s.state_name << "\n";
                        }
                    }
                }
            }
        }
        /*
            we have now determined whether to change state or not and in the case where we are
            going to change state we reset subcondition flags (ref TAG keyword) on all states
            that have them except the newly active state
        */
        for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
            StableState &s = stable_states[ss_idx];
            // this state is not active so ensure its subcondition flags are turned off
            if (s.subcondition_handlers && changed_state && active_state != &s) {
                std::list<ConditionHandler>::iterator iter = s.subcondition_handlers->begin();
                while (iter != s.subcondition_handlers->end()) {
                    ConditionHandler &ch = *iter++;
                    if (ch.command_name() == "FLAG") {
                        MachineInstance *flag = lookup(ch.flag_name());
                        if (flag) {
                            const State *off = flag->state_machine->findState("off");
                            if (strcmp("off", flag->getCurrentStateString())) {
                                DBG_AUTOSTATES << "turning flag off since the state "
                                               << s.state_name << " is not active\n";
                                flag->setState(*off);
                            }
                        }
                        else {
                            std::cerr << _name << " error: flag " << ch.flag_name()
                                      << " not found\n";
                        }
                    }
                }
            }
        }
        if (ptd) {
            Trigger *trigger = new Trigger(this, ptd->label);
            Scheduler::instance()->add(new ScheduledItem(ptd->delay, trigger));
            trigger->release();
            delete ptd;
        }
    }
    return changed_state;
}

void MachineInstance::setStateMachine(MachineClass *machine_class) {
    //if (state_machine) return;
    state_machine = machine_class;
    DBG_PARSER << _name << " is of class " << machine_class->name << "\n";
    if (machine_class && machine_class->name == "COMMANDCLOCK") {
        std::unique_lock<std::mutex> lock(global_lists_mutex);
        registerCommandClockLocked();
        command_clock_cache_valid = false;
    }
    if (machine_class && RecordClass::isRow(machine_class)) {
        std::unique_lock<std::mutex> lock(global_lists_mutex);
        bool already = false;
        std::list<MachineInstance *>::iterator it = record_instances.begin();
        while (it != record_instances.end()) {
            if (*it++ == this) {
                already = true;
                break;
            }
        }
        if (!already) {
            record_instances.push_back(this);
        }
        // RECORD owns empty/dirty/clean (runtime setState); a table-bound MACHINE
        // owns its own state via WHEN, so do not setState on it.
        if (RecordClass::isRecord(machine_class)) {
            setState(machine_class->initial_state);
        }
    }
    if (my_instance_type == MACHINE_INSTANCE && machine_class->allow_auto_states &&
        (machine_class->stable_states.size() || machine_class->name == "LIST" ||
         machine_class->name == "REFERENCE" ||
         (machine_class->plugin && machine_class->plugin->state_check))) {
        automatic_machines.push_back(this);
    }
    for (StableState &s : machine_class->stable_states) {
        stable_states.push_back(s);
        stable_states[stable_states.size() - 1].setOwner(this); //
    }
    std::pair<Message, MachineCommandTemplate *> handler;
    for (auto &handler : machine_class->receives) {
        //DBG_M_INITIALISATION << "setting receive handler for " << handler.first << "\n";
        MachineCommand *mc = new MachineCommand(this, handler.second);
        receives_functions.insert(std::make_pair(handler.first, mc));
    }
    // Warning: enter_functions are not used. TBD
    for (auto &handler : machine_class->enter_functions) {
        MachineCommand *mc = new MachineCommand(this, handler.second);
        mc->retain();
        enter_functions[handler.first] = mc;
    }
    std::pair<std::string, MachineCommandTemplate *> node;
    for (auto &node : state_machine->commands) {
        MachineCommand *mc = new MachineCommand(this, node.second);
        mc->retain();
        commands.insert(std::make_pair(node.first, mc));
    }
    std::pair<std::string, Value> option;
    for (auto &option : machine_class->getOptions()) {
        DBG_INITIALISATION << _name << " initialising property " << option.first << " ("
                           << option.second << ")\n";
        if (option.second.kind == Value::t_symbol) {
            Value v = getValue(option.second.sValue);
            if (v != SymbolTable::Null) {
                properties.add(option.first, v, SymbolTable::NO_REPLACE);
            }
            else {
                properties.add(option.first, option.second, SymbolTable::NO_REPLACE);
            }
        }
        else {
            properties.add(option.first, option.second, SymbolTable::NO_REPLACE);
        }
        if (option.first == "POLLING_DELAY") {
            int64_t pd;
            if (option.second.asInteger(pd)) {
                idle_time = pd;
            }
        }
    }
    // clone properties from the class into this instance but don't replace
    // properties already loaded
    properties.add(state_machine->getProperties(), SymbolTable::NO_REPLACE);
    properties.add("NAME", _name.c_str(), SymbolTable::ST_REPLACE);
    if (locals.size() == 0) {
        for (Parameter p : state_machine->locals) {
            Parameter newp(p.val.sValue.c_str());
            if (!p.machine) {
                MessageLog::instance()->add(_name,
                                            ": no clonable instance for parameter: ", p.val.sValue);
                DBG_M_INITIALISATION << "failed to clone. no instance defined for parameter "
                                     << p.val.sValue << "\n";
            }
            else {
                newp.machine =
                    MachineInstanceFactory::create(p.val.sValue.c_str(), p.machine->_type.c_str());
                newp.machine->setProperties(p.machine->properties);
                newp.machine->setDefinitionLocation(p.machine->definition_file.c_str(),
                                                    p.machine->definition_line);
                listenTo(newp.machine);
                newp.machine->addDependancy(this);
                auto c_iter = MachineClass::machine_classes.find(newp.machine->_type);
                if (c_iter == MachineClass::machine_classes.end()) {
                    auto &ss = MessageLog::instance()->get_stream();
                    ss << _name << ": " << newp.machine->_type << " not found";
                    auto error = MessageLog::instance()->access_stream_message();
                    error_messages.push_back(error);
                    DBG_M_INITIALISATION << error << "\n";
                    MessageLog::instance()->release_stream();
                    ++num_errors;
                }
                else if ((*c_iter).second) {
                    newp.machine->owner = this;
                    MachineClass *newsm = (*c_iter).second;
                    newp.machine->setStateMachine(newsm);
                    if (newsm->parameters.size() != p.machine->parameters.size()) {
                        // LISTS can have any number of parameters
                        // POINTs and ANALOGINPUTs can have 2 or three parameters
                        if (newsm->name == "LIST") {
                            DBG_PARSER << "List has " << p.machine->parameters.size()
                                       << " parameters\n";
                            p.machine->setNeedsCheck();
                        }
                        if (newsm->name == "LIST" ||
                            ((newsm->name == "POINT" ||
                              newsm->name == "COUNTER" || newsm->name == "INPUTBIT" ||
                              newsm->name == "OUTPUTBIT" || newsm->name == "INPUTREGISTER" ||
                              newsm->name == "OUTPUTREGISTER" || newsm->name == "DIGITALVALUE" ||
                              newsm->name == "ANALOGOUTPUT" || newsm->name == "STATUS_FLAG") &&
                             p.machine->parameters.size() >= 2 &&
                             p.machine->parameters.size() <= 4) ||
                            (newsm->name == "ANALOGINPUT" &&
                             p.machine->parameters.size() >= 2 &&
                             p.machine->parameters.size() <= 5) ||
                            (newsm->name == "COUNTERRATE" &&
                             (p.machine->parameters.size() == 1 ||
                              (p.machine->parameters.size() >= 3 &&
                               p.machine->parameters.size() <= 5)))) {
                        }
                        else {
                            resetTemporaryStringStream();
                            ss << "## - Error: Machine " << newsm->name << " requires "
                               << newsm->parameters.size() << " parameters but instance " << _name
                               << "." << newp.machine->getName() << " has "
                               << p.machine->parameters.size();
                            error_messages.push_back(ss.str());
                            ++num_errors;
                        }
                    }
                }
                if (p.machine->parameters.size()) {
                    std::copy(p.machine->parameters.begin(), p.machine->parameters.end(),
                              back_inserter(newp.machine->parameters));
                    DBG_M_INITIALISATION << "copied " << p.machine->parameters.size()
                                         << " parameters. local has "
                                         << p.machine->parameters.size() << "parameters\n";
                }
                if (p.machine->stable_states.size()) {
                    DBG_M_INITIALISATION << " restoring stable states for " << newp.val
                                         << "...before: " << newp.machine->stable_states.size();
                    newp.machine->stable_states.clear();
                    BOOST_FOREACH (StableState &s, p.machine->stable_states) {
                        newp.machine->stable_states.push_back(s);
                        newp.machine->stable_states[newp.machine->stable_states.size() - 1]
                            .setOwner(this); //
                    }
                    std::copy(p.machine->stable_states.begin(), p.machine->stable_states.end(),
                              back_inserter(newp.machine->stable_states));
                    DBG_M_INITIALISATION << " after: " << newp.machine->stable_states.size()
                                         << "\n";
                }
            }
            locals.push_back(newp);
        }
    }
    else {
        NB_MSG << _name << " has locals, state machine not initialised\n";
    }
    // a list has many parameters but the list class does not.
    // nevertheless, the parameters and dependencies still need to be setup.
    if (state_machine->token_id == ClockworkToken::LIST) {
        for (unsigned int i = 0; i < parameters.size(); ++i) {
            parameters[i].real_name = parameters[i].val.sValue;
            MachineInstance *m = lookup(parameters[i].real_name);
            if (m) {
                DBG_M_INITIALISATION << " found " << m->getName() << "\n";
                m->addDependancy(this);
                listenTo(m);
            }
            parameters[i].machine = m;
            setNeedsCheck();
            fixListState(*this);
        }
    }
    // TBD check the parameter types
    size_t num_class_params = state_machine->parameters.size();
    for (unsigned int i = 0; i < parameters.size(); ++i) {
        if (i < num_class_params) {
            if (parameters[i].val.kind == Value::t_symbol) {
                parameters[i].real_name = parameters[i].val.sValue;
                parameters[i].val = state_machine->parameters[i].val.sValue.c_str();
                MachineInstance *m = lookup(parameters[i].real_name);
                DBG_M_INITIALISATION << _name << " is looking up " << parameters[i].real_name
                                     << " for param " << i << "\n";
                if (m) {
                    DBG_M_INITIALISATION << " found " << m->getName() << "\n";
                    m->addDependancy(this);
                    listenTo(m);
                }
                parameters[i].machine = m;
            }
            else {
                DBG_M_MSG << "Parameter " << i << " (" << parameters[i].val << ") with real name "
                          << state_machine->parameters[i].val << "\n";
            }
            // fix the properties for the machine passed as a parameter
            // the statemachine may nominate a default property that isn't
            // provided by the actual parameter. here we find these situations
            // and set the default
            if (parameters[i].machine && !state_machine->parameters[i].properties.empty()) {
                SymbolTableConstIterator st_iter = state_machine->parameters[i].properties.begin();
                while (st_iter != state_machine->parameters[i].properties.end()) {
                    std::pair<std::string, Value> prop = *st_iter++;
                    if (!parameters[i].machine->properties.exists(prop.first.c_str())) {
                        DBG_M_INITIALISATION << "copying default property " << prop.first
                                             << " on parameter " << i;
                        DBG_M_INITIALISATION << " to object" << *(parameters[i].machine) << "\n";
                        parameters[i].machine->properties.add(prop.first, prop.second,
                                                              SymbolTable::NO_REPLACE);
                    }
                    else {
                        DBG_M_INITIALISATION << "default property " << prop.first
                                             << " overridden by value ";
                        DBG_M_INITIALISATION
                            << parameters[i].machine->properties.lookup(prop.first.c_str()) << "\n";
                    }
                }
            }
        }
    }
    // copy transitions to support transtions on receipt of events from other machines
    BOOST_FOREACH (Transition &transition, state_machine->transitions) {
        transitions.push_back(transition);

        // copy this transition with the real name of the machine for the trigger
        std::string machine = transition.trigger.getText();
        if (machine.find('.') != std::string::npos) {
            machine.erase((machine.find('.')));
            // if this is a parameter, copy the transition to use the real name
            for (unsigned int i = 0; i < state_machine->parameters.size(); ++i) {
                if (parameters[i].val == machine) {
                    std::string evt = transition.trigger.getText();
                    evt = evt.substr(evt.find('.') + 1);
                    std::string trigger = parameters[i].real_name + "." + evt;
                    transitions.push_back(
                        Transition(transition.source, transition.dest, Message(trigger.c_str())));
                }
            }
        }
    }

    setupModbusInterface();
}

std::ostream &operator<<(std::ostream &out, const Action &a) { return a.operator<<(out); }

Trigger *MachineInstance::setupTrigger(const std::string &machine_name, const std::string &message,
                                       const char *suffix = "_enter") {
    std::string trigger_name = machine_name;
    trigger_name += ".";
    trigger_name += message;
    trigger_name += suffix;
    DBG_M_MESSAGING << _name << " is waiting for message " << trigger_name << " from "
                    << machine_name << "\n";
    return new Trigger(this, trigger_name);
}

const Value *MachineInstance::resolve(std::string property) {
    if (property.find('.') != std::string::npos) {
        // property is on another machine
        std::string name = property;
        name.erase(name.find('.'));
        std::string prop = property.substr(property.find('.') + 1);
        MachineInstance *other = lookup(name);
        if (other) {
            return other->resolve(prop);
        }
        else if (state_machine->token_id == ClockworkToken::REFERENCE && name == "ITEM" &&
                 locals.size() == 0) {
            // permit references items to be not always available so that these can be
            // added and removed as the program executes
            return &SymbolTable::Null;
        }
        else {
            resetTemporaryStringStream();
            ss << fullName() << " resolve() could not find machine named " << name
               << " for property " << property;
            error_messages.push_back(ss.str());
            ++num_errors;
            DBG_MSG << ss.str() << "\n";
            MessageLog::instance()->add(ss.str().c_str());
            NB_MSG << ss.str() << "\n";
            return &SymbolTable::Null;
        }
    }
    else {
        // try the current machine's parameters, the current instance of the machine, then the machine class and finally the global symbols
        const Value *res = 0;
        Value property_val(property); // tokenise the property
        if (property == "TIMEOUT") {
            // timeout-spec.md: TIMEOUT is a read-only contextual value, valid
            // only while executing an ON TIMEOUT block.
            return getTimeoutContextValue();
        }
        if (property_val.token_id == ClockworkToken::TIMER) {
            // we do not use the precalculated timer here since this may be being accessed
            // within an action handler of a nother machine and will not have been updated
            // since the last evaluation of stable states.
            return getTimerVal();
        }
        // variables may refer to an initialisation value passed in as a parameter.
        if (state_machine->token_id == ClockworkToken::VARIABLE ||
            state_machine->token_id == ClockworkToken::CONSTANT) {
            for (unsigned int i = 0; i < parameters.size(); ++i) {
                if (state_machine->parameters[i].val.kind == Value::t_symbol &&
                    property == state_machine->parameters[i].val.sValue) {
                    DBG_M_PREDICATES << _name << " found parameter " << i << " to resolve "
                                     << property << "\n";
                    return &parameters[i].val;
                }
            }
        }
        // use the global value if its name is mentioned in this machine's list of globals
        if (!state_machine) {
            resetTemporaryStringStream();
            ss << _name << " could not find a machine definition: " << _type;
            DBG_PROPERTIES << ss.str() << "\n";
            error_messages.push_back(ss.str());
            MessageLog::instance()->add(ss.str().c_str());
            ++num_errors;
            return &SymbolTable::Null;
        }
        else if (state_machine->global_references.count(property)) {
            MachineInstance *m = state_machine->global_references[property];
            if (m) {
                const Value *global_property_val = &m->properties.lookup("VALUE");
                if (*global_property_val == SymbolTable::Null) {
                    DBG_M_PROPERTIES << _name << " using current state from global "
                                     << m->fullName() << "\n";
                    return &m->current_state_val;
                }
                else {
                    DBG_M_PROPERTIES << _name << " using value property " << m->getValue("VALUE")
                                     << " from global " << m->fullName() << "\n";
                    return global_property_val;
                }
            }
            else {
                resetTemporaryStringStream();
                ss << fullName() << " failed to find the machine for global: " << property;
                DBG_PROPERTIES << ss.str() << "\n";
                error_messages.push_back(ss.str());
                MessageLog::instance()->add(ss.str().c_str());
                ++num_errors;
            }
        }
        DBG_M_PROPERTIES << getName() << " MachineInstance::resolve() looking up property "
                         << property << "\n";
        if ((res = lookupState(property_val)) != &SymbolTable::Null) {
            return res;
        }
        else if (SymbolTable::isKeyword(property_val)) {
            return &SymbolTable::getKeyValue(property.c_str());
        }
        else {
            const Value *res = &properties.lookup(property.c_str());
            if (*res == SymbolTable::Null) {
                if (state_machine) {
                    const Value *class_property =
                        &state_machine->getProperties().lookup(property.c_str());
                    if (class_property == &SymbolTable::Null) {
                        DBG_M_PROPERTIES << "MachineInstance::resolve: no property " << property
                                         << " found in class, looking in globals\n";
                        if (globals.exists(property.c_str())) {
                            return &globals.lookup(property.c_str());
                        }
                    }
                }
            }
            else {
                DBG_M_PROPERTIES << "found property " << property << " (type: " << res->kind << ") "
                                 << res->asString() << "\n";
                return res;
            }
        }

        // finally, the 'property' may be a VARIABLE or CONSTANT machine declared locally
        MachineInstance *m = lookup(property);
        if (m) {
            if (m->state_machine && (m->state_machine->token_id == ClockworkToken::VARIABLE ||
                                     m->state_machine->token_id == ClockworkToken::CONSTANT)) {
                return &m->properties.lookup("VALUE");
            }
            else if (m->getStateMachine()) {

                if (m->getStateMachine()->token_id == ClockworkToken::VARIABLE ||
                    m->getStateMachine()->token_id == ClockworkToken::CONSTANT) {
                    return &m->properties.lookup("VALUE");
                }
                else if (m->getStateMachine()->token_id == ClockworkToken::LIST ||
                         m->getStateMachine()->token_id == ClockworkToken::REFERENCE) {
                    return m->getCurrent().getNameValue();
                }
            }
            //return new Value(new MachineValue(m, property));
            return &m->current_value_holder;
        }
    }
    return &SymbolTable::Null;
}

const Value *MachineInstance::getValuePtr(Value &property) {
    if (property.cached_value) {
        return property.cached_value;
    }
    assert(property.kind == Value::t_symbol || property.kind == Value::t_string);
    Value *res = getMutableValue(property.sValue.c_str());
    if (res) {
        property.cached_value = res;
        return res;
    }
    return &SymbolTable::Null;
}

const Value &MachineInstance::getValue(Value &property) {
    if (property.cached_value) {
        return *property.cached_value;
    }
    assert(property.kind == Value::t_symbol || property.kind == Value::t_string);
    Value *res = getMutableValue(property.sValue.c_str());
    if (res) {
        property.cached_value = res;
        return *res;
    }
    return SymbolTable::Null;
}

const Value &MachineInstance::getValue(const char *property_name) {
    std::string property(property_name);
    return getValue(property);
}

const Value &MachineInstance::getValue(const std::string &property) {
    if (property.find('.') != std::string::npos) {
        // property is on another machine
        std::string name = property;
        name.erase(name.find('.'));
        std::string prop = property.substr(property.find('.') + 1);
        MachineInstance *other = lookup(name);
        if (other) {
            const Value &v = other->getValue(prop);
            DBG_M_PROPERTIES << other->getName() << " found property " << prop
                             << " (type: " << v.kind << ") " << " in machine " << name
                             << " with value " << v << "\n";
            return v;
        }
        else if (state_machine->token_id == ClockworkToken::REFERENCE && name == "ITEM" &&
                 locals.size() == 0) {
            // permit references items to be not always available so that these can be
            // added and removed as the program executes
            return SymbolTable::Null;
        }
        else {
            resetTemporaryStringStream();
            ss << _name << " getValue() could not find machine named " << name << " for property "
               << property;
            error_messages.push_back(ss.str());
            ++num_errors;
            DBG_MSG << ss.str() << "\n";
            MessageLog::instance()->add(ss.str().c_str());
            return SymbolTable::Null;
        }
    }
    else {
        Value property_val(property); // use this to avoid string comparisions on property

        // try the current machine's parameters, the current instance of the machine, then the machine class and finally the global symbols

        // variables may refer to an initialisation value passed in as a parameter.
        if (state_machine && (state_machine->token_id == ClockworkToken::VARIABLE ||
                              state_machine->token_id == ClockworkToken::CONSTANT)) {
            for (unsigned int i = 0; i < parameters.size(); ++i) {
                if (state_machine->parameters[i].val.kind == Value::t_symbol &&
                    property == state_machine->parameters[i].val.sValue

                ) {
                    DBG_M_PREDICATES << _name << " found parameter " << i << " to resolve "
                                     << property << "\n";
                    return parameters[i].val;
                }
            }
        }
        // use the global value if its name is mentioned in this machine's list of globals
        if (!state_machine) {
            resetTemporaryStringStream();
            ss << _name << " could not find a machine definition: " << _type;
            DBG_PROPERTIES << ss.str() << "\n";
            error_messages.push_back(ss.str());
            ++num_errors;
        }
        else if (state_machine->global_references.count(property)) {
            MachineInstance *m = state_machine->global_references[property];
            if (m) {
                DBG_M_PROPERTIES << _name << " using value property " << m->getValue("VALUE")
                                 << " from " << m->getName() << "\n";
                return m->getValue("VALUE");
            }
            else {
                resetTemporaryStringStream();
                ss << fullName() << " failed to find the machine for global: " << property;
                DBG_PROPERTIES << ss.str() << "\n";
                error_messages.push_back(ss.str());
                MessageLog::instance()->add(ss.str().c_str());
                ++num_errors;
            }
        }
        DBG_M_PROPERTIES << getName() << " MachineInstance::getValue() looking up property "
                         << property << "\n";
        if (property_val.token_id == ClockworkToken::TIMER) {
            // we do not use the precalculated timer here since this may be being accessed
            // within an action handler of a nother machine and will not have been updated
            // since the last evaluation of stable states.

            //DBG_M_PROPERTIES << getName() << " timer: " << state_timer << "\n";
            //return state_timer;
            return *getTimerVal();
        }
        else if (SymbolTable::isKeyword(property.c_str())) {
            return SymbolTable::getKeyValue(property.c_str());
        }
        else {
            const Value &x = properties.lookup(property.c_str());
            if (x == SymbolTable::Null) {
                if (state_machine) {
                    if (!state_machine->getProperties().exists(property.c_str())) {
                        DBG_M_PROPERTIES << getName() << " MachineInstance::getValue no property "
                                         << property << " found in class, looking in globals\n";
                        if (globals.exists(property.c_str())) {
                            DBG_PROPERTIES << _name << " found global for " << property << "\n";
                            return globals.lookup(property.c_str());
                        }
                    }
                    else {
                        DBG_M_PROPERTIES << "using property " << property << "from class\n";
                        return state_machine->getProperties().lookup(property.c_str());
                    }
                }
            }
            else {
                DBG_M_PROPERTIES << " found property " << property << " (type: " << x.kind << ") "
                                 << x.asString() << "\n";
                return x;
            }
        }

        // finally, the 'property' may be a VARIABLE or CONSTANT machine declared locally
        MachineInstance *m = lookup(property);
        if (m) {
            if (m->state_machine->token_id == ClockworkToken::VARIABLE ||
                m->state_machine->token_id == ClockworkToken::CONSTANT) {
                return m->getValue("VALUE");
            }
            // we no longer support the idea that the property lookup process may return the state of a machine
            //else return *m->getCurrentStateVal();
        }
    }
    return SymbolTable::Null;
}

Value *MachineInstance::getMutableValue(const char *property_name) {
    std::string property(property_name);
    if (property.find('.') != std::string::npos) {
        // property is on another machine
        std::string name = property;
        name.erase(name.find('.'));
        std::string prop = property.substr(property.find('.') + 1);
        MachineInstance *other = lookup(name);
        if (other) {
            Value *v = other->getMutableValue(prop.c_str());
            DBG_M_PROPERTIES << other->getName() << " found property " << prop
                             << " (type: " << v->kind << ") in machine " << name << " with value "
                             << *v << "\n";
            return v;
        }
        else if (state_machine->token_id == ClockworkToken::REFERENCE && name == "ITEM" &&
                 locals.size() == 0) {
            // permit references items to be not always available so that these can be
            // added and removed as the program executes
            return 0;
        }
        else {
            resetTemporaryStringStream();
            ss << _name << " getValue() could not find machine named " << name << " for property "
               << property;
            error_messages.push_back(ss.str());
            ++num_errors;
            DBG_MSG << ss.str() << "\n";
            MessageLog::instance()->add(ss.str().c_str());
            return 0;
        }
    }
    else {
        Value property_val(property); // use this to avoid string comparisions on property

        // try the current machine's parameters, the current instance of the machine, then the machine class and finally the global symbols

        // variables may refer to an initialisation value passed in as a parameter.
        if (state_machine && (state_machine->token_id == ClockworkToken::VARIABLE ||
                              state_machine->token_id == ClockworkToken::CONSTANT)) {
            for (unsigned int i = 0; i < parameters.size(); ++i) {
                if (state_machine->parameters[i].val.kind == Value::t_symbol &&
                    property == state_machine->parameters[i].val.sValue

                ) {
                    DBG_M_PREDICATES << _name << " found parameter " << i << " to resolve "
                                     << property << "\n";
                    return &parameters[i].val;
                }
            }
        }
        // use the global value if its name is mentioned in this machine's list of globals
        if (!state_machine) {
            resetTemporaryStringStream();
            ss << _name << " could not find a machine definition: " << _type;
            DBG_PROPERTIES << ss.str() << "\n";
            error_messages.push_back(ss.str());
            ++num_errors;
        }
        else if (state_machine->global_references.count(property)) {
            MachineInstance *m = state_machine->global_references[property];
            if (m) {
                DBG_M_PROPERTIES << _name << " using value property " << m->getValue("VALUE")
                                 << " from " << m->getName() << "\n";
                return m->getMutableValue("VALUE");
            }
            else {
                resetTemporaryStringStream();
                ss << fullName() << " failed to find the machine for global: " << property;
                DBG_PROPERTIES << ss.str() << "\n";
                error_messages.push_back(ss.str());
                MessageLog::instance()->add(ss.str().c_str());
                ++num_errors;
            }
        }
        DBG_M_PROPERTIES << getName() << " looking up property " << property << "\n";
        if (property_val.token_id == ClockworkToken::TIMER) {
            assert(false);
        }
        else if (SymbolTable::isKeyword(property.c_str())) {
            assert(false);
        }
        else {
            Value &x = properties.find(property.c_str());
            if (x == SymbolTable::Null) {
                if (state_machine) {
                    if (!state_machine->getProperties().exists(property.c_str())) {
                        DBG_M_PROPERTIES << "no property " << property
                                         << " found in class, looking in globals\n";
                        if (globals.exists(property.c_str())) {
                            return &globals.find(property.c_str());
                        }
                    }
                    else {
                        assert(false);
                    }
                }
            }
            else {
                DBG_M_PROPERTIES << "3526 found property " << property << " (type: " << x.kind
                                 << ") " << x.asString() << "\n";
                return &x;
            }
        }

        // finally, the 'property' may be a VARIABLE or CONSTANT machine declared locally
        MachineInstance *m = lookup(property);
        if (m) {
            if (m->state_machine->token_id == ClockworkToken::VARIABLE ||
                m->state_machine->token_id == ClockworkToken::CONSTANT) {
                return m->getMutableValue("VALUE");
            }
            //else
            //  return m->getCurrentStateVal();
        }
    }
    return 0;
}

const Value *MachineInstance::lookupState(const std::string &state_name) {
    //is state_name a valid state?
    if (state_machine) {
        std::list<State *>::iterator iter = state_machine->states.begin();
        while (iter != state_machine->states.end()) {
            State *s = *iter++;
            if (s->getName() == state_name) {
                return s->getNameValue();
            }
        }
        for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
            if (stable_states[ss_idx].state_name == state_name) {
                return &stable_states[ss_idx].name;
            }
        }
    }
    return &SymbolTable::Null;
}

const Value *MachineInstance::lookupState(const Value &state_name) {
    //is state_name a valid state?
    if (state_machine) {
        std::list<State *>::iterator iter = state_machine->states.begin();
        while (iter != state_machine->states.end()) {
            State *s = *iter++;
            if (s->getId() == state_name.token_id) {
                return s->getNameValue();
            }
        }
        for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
            if (stable_states[ss_idx].name == state_name) {
                return &stable_states[ss_idx].name;
            }
        }
    }
    return &SymbolTable::Null;
}

bool MachineInstance::hasState(const State &test) const {
    if (state_machine) {
        const State *s = state_machine->findState(test);
        if (s) {
            return true;
        }
    }
    else {
        char buf[100];
        snprintf(buf, 100, "warning: %s does not have a state; %s", fullName().c_str(),
                 test.getName().c_str());
        MessageLog::instance()->add(buf);
    }
    return false;
}

bool MachineInstance::hasState(const std::string &state_name) const {
    //is state_name a valid state?
    State s(state_name.c_str());
    return hasState(s);
}

// change batching
bool MachineInstance::changing() { return is_changing; }

bool MachineInstance::prepare() {
    is_changing = true;
    return is_changing;
}

void MachineInstance::commit() {
    setNeedsCheck();
    notifyDependents();
    std::map<std::string, Value>::iterator iter = changes.begin();
    while (iter != changes.end()) {
        setValue((*iter).first, (*iter).second);
        iter = changes.erase(iter);
    }
    is_changing = false;
}

void MachineInstance::discard() {
    changes.clear();
    is_changing = false;
}

bool MachineInstance::isPersistent() {
    const Value &persistent = getValue("PERSISTENT");
    if (persistent != SymbolTable::Null && persistent == "true") {
        return true;
    }
    // PERSISTENT OPTION: a class with any per-field persistent OPTION is also
    // persisted (only those fields), even without the machine-level flag.
    return state_machine && state_machine->hasPersistentProperties();
}

void MachineInstance::sendModbusUpdate(const std::string &property_name, const Value &new_value) {
    if (false) {
        FileLogger fl(program_name);
        fl.f() << _name << " Sending modbus update " << property_name << " " << new_value
               << " (kind:" << new_value.kind << ")" << "\n";
    }

    ModbusAddress ma = modbus_exports[property_name];
    switch (ma.getGroup()) {
    case ModbusAddress::none:
        DBG_M_MODBUS << property_name << " export type is 'none'\n";
        break;
    case ModbusAddress::discrete:
    case ModbusAddress::coil:
    case ModbusAddress::input_register:
    case ModbusAddress::holding_register: {
        if (new_value.kind == Value::t_float) {
            double floatVal;
            if (ma.length() == 1 || ma.length() == 2) {
                if (new_value.asFloat(floatVal)) {
                    ma.update(this, floatVal);
                }
                else {
                    DBG_M_MODBUS << property_name << " does not have an integer value\n";
                }
            }
        }
        else if (new_value.kind == Value::t_integer) {
            int64_t intVal;
            if (ma.length() == 1 || ma.length() == 2) {
                if (new_value.asInteger(intVal)) {
                    ma.update(this, (int)intVal);
                }
                else {
                    DBG_M_MODBUS << property_name << " does not have an integer value\n";
                }
            }
        }
        else if (new_value.kind == Value::t_string || new_value.kind == Value::t_symbol) {
            long val;
            char *res;
            val = strtol(new_value.sValue.c_str(), &res, 10);
            if (errno == 0 && *res == 0) { // complete string parsed as a number
                ma.update(this, (int)val);
            }
            else {
                ma.update(this, new_value.sValue);
            }
        }
        else {
            DBG_M_MODBUS << "unable to export " << property_name << "\n";
        }
    } break;
    case ModbusAddress::string: {
        ma.update(this, new_value.asString());
    }
    }
}

bool MachineInstance::setValue(const std::string &property, const Value &new_value,
                               uint64_t authority) {

    if (property.length() == 0) {
        char buf[100];
        snprintf(buf, 100, "%s: attempt to set property with empty name", _name.c_str());
        MessageLog::instance()->add(buf);
        DBG_MSG << buf << "\n";
        return false;
    }

    if (property == "notify_period" || property == "command" || property == "notify_phase") {
        command_clock_cache_valid = false;
    }

    DBG_M_PROPERTIES << _name << " setvalue " << property << " to " << new_value << "\n";
    if (property.find('.') != std::string::npos) {
        // property is on another machine
        std::string name = property;
        name.erase(name.find('.'));
        std::string prop = property.substr(property.find('.') + 1);
        MachineInstance *other = lookup(name);
        if (other) {
            if (prop.length()) {
                return other->setValue(prop, new_value);
            }
            else {
                char buf[100];
                snprintf(buf, 100, "%s bad request to set property %s", _name.c_str(),
                         property.c_str());
                MessageLog::instance()->add(buf);
                DBG_MSG << buf << "\n";
                return false;
            }
        }
        else {
            char buf[150];
            snprintf(buf, 150, "%s  setValue() could not find machine named %s for property %s",
                     _name.c_str(), name.c_str(), property.c_str());
            MessageLog::instance()->add(buf);
            DBG_PROPERTIES << buf << "\n";
            return false;
        }
    }
    else {

        // This expected authority facility ensures that only
        // authorised sources are able to update a property
        // In the case of a shadow, if the current method
        // was not called with the
        // right authority, the request is forwarded to
        // the channel that owns the current machine.
        if (expected_authority != 0 && authority == 0) { //expected_authority != authority ) {
            //FileLogger fl(program_name);
            //fl.f() << _name << " refused to set property " << property << " to " << new_value << " due to authority mismatch. "
            //<< " needed: " << expected_authority << " got " << authority << "\n";
            if (isShadow()) {
                Channel *chn = ownerChannel();
                if (chn && chn->current_state != ChannelImplementation::DISCONNECTED) {
                    chn->sendPropertyChangeMessage(this, fullName(), property, new_value,
                                                   authority);
                    return true;
                    //fl.f() << _name << "forwarding property change request to owner channel\n";
                }
                else if (chn) {
                    //fl.f() << _name << "cannot forward property change request because the channel is disconnected\n";
                }
            }
            return false;
        }

        Value property_val(property); // use this value in comparisons for performance
        bool was_changed = false;

        if (property_val.token_id == ClockworkToken::POLLING_DELAY) {
            int64_t new_delay = 0;
            if (new_value.asInteger(new_delay)) {
                idle_time = new_delay;
            }
            if (state_machine->token_id == ClockworkToken::SYSTEMSETTINGS) {
                *MachineInstance::polling_delay = new_delay;
                set_polling_time(static_cast<unsigned long>(new_delay > 0 ? new_delay : 100));
                was_changed = true;
            }
        }
        else if (property_val.token_id == ClockworkToken::CYCLE_DELAY) {
            // SYSTEM.CYCLE_DELAY = EtherCAT bus period (µs). Processing uses POLLING_DELAY.
            int64_t new_delay = 0;
            if (new_value.asInteger(new_delay) &&
                state_machine->token_id == ClockworkToken::SYSTEMSETTINGS) {
#ifndef EC_SIMULATOR
                // iod-elc: lock-at-activate helper (ignore live retune after arm).
                ECInterface::instance()->applyCyclePeriodUs(
                    static_cast<unsigned long>(new_delay > 0 ? new_delay : 100));
#endif
                was_changed = true;
            }
        }
        else if (property_val.token_id == ClockworkToken::TRACEABLE) {
            if (new_value == "TRUE") {
                is_traceable = true;
            }
            if (new_value == "FALSE") {
                is_traceable = false;
            }
            return true; // special case, short-circuit other tests
        }

        // often variables are VALUE properties in named machines in the GLOBAL list
        std::map<std::string, MachineInstance *>::const_iterator global_var =
            state_machine->global_references.find(property);
        if (global_var != state_machine->global_references.end()) {
            MachineInstance *global_machine = (*global_var).second;
            if (global_machine &&
                global_machine->state_machine->token_id != ClockworkToken::CONSTANT) {
                DBG_PROPERTIES << global_machine->getName() << " setting property VALUE to "
                               << new_value << "\n";
                return global_machine->setValue("VALUE", new_value);
            }
            NB_MSG << "attempt to set a value on global constant " << property << ". ignored\n";
            return false;
        }
        // try the current instance ofthe machine, then the machine class and finally the global symbols
        DBG_PROPERTIES << getName() << " setting property " << property << " to " << new_value
                       << "\n";
        const Value &prev_value = properties.lookup(property.c_str());

        if (prev_value == SymbolTable::Null && property_val.token_id != ClockworkToken::tokVALUE &&
            property != _name) {
            // the 'property' may be a VARIABLE or CONSTANT machine declared locally or globally
            MachineInstance *global_machine = lookup(property);
            if (global_machine) {
                if (global_machine->state_machine->token_id != ClockworkToken::CONSTANT) {
                    return global_machine->setValue("VALUE", new_value);
                }
                NB_MSG << "attempt to set a value on constant " << property << ". ignored\n";
                return false;
            }
        }

        if ((new_value.kind == Value::t_integer || new_value.kind == Value::t_float) &&
            state_machine && state_machine->plugin && state_machine->plugin->filter) {
            double float_value = new_value.kind == Value::t_float ? new_value.fValue : new_value.iValue;
            double filtered_value = state_machine->plugin->filter(this, float_value);
            was_changed = (!prev_value.identical(filtered_value) ||
                           (new_value != SymbolTable::Null && prev_value == SymbolTable::Null));
            if (was_changed) {
                properties.add(property, filtered_value, SymbolTable::ST_REPLACE);
            }
        }
        else {
            was_changed = (!prev_value.identical(new_value) || prev_value.kind != new_value.kind ||
                           (new_value != SymbolTable::Null && prev_value == SymbolTable::Null));
            if (was_changed) {
                properties.add(property, new_value, SymbolTable::ST_REPLACE);
            }
        }
        if (!was_changed) {
            return true; // value was ok but was already the same
        }
        if (RecordClass::isRecord(state_machine) && !record_apply_mode &&
            !state_machine->propertyIsLocal(property) && current_state.getName() != "dirty") {
            setState("dirty");
        }
        // calcAdjust temps: one notify at endDeferredPropertyNotify.
        // Never defer VALUE / IO (outputs, plugins, analog owners).
        if (property_notify_defer > 0 && !io_interface &&
            property_val.token_id != ClockworkToken::tokVALUE) {
            deferred_property_notify = true;
            return true;
        }
#ifndef EC_SIMULATOR
#ifdef USE_SDO
        if (property_val.token_id == ClockworkToken::tokVALUE && _type == "SDOENTRY") {
            SDOEntry *entry = SDOEntry::find(_name);
            if (entry) {
                Value io_value = entry->readValue();
                if (entry->ready() && io_value != new_value) {
                    DBG_MSG << "updating " << _name << " io: " << io_value << " new: " << new_value
                            << "\n";
                    ECInterface::instance()->queueInitialisationRequest(entry, new_value);
                }
            }
        }
        else
#endif //USE_SDO
#endif
            if (property_val.token_id == ClockworkToken::tokVALUE && io_interface) {
            char buf[100];
            errno = 0;
            int64_t value = 0; // TBD deal with sign
            if (new_value.asInteger(value)) {
                // Inputs: VALUE is published from the filter / sample path.
                // Writing the process image would treat an ANALOGINPUT/POINT as
                // an output and corrupt PDO state.
                if (io_interface->direction() != IOComponent::DirInput) {
                    if (io_interface->address.is_signed) {
                        io_interface->setValue((int32_t)(value & 0xffffffff));
                    }
                    else {
                        io_interface->setValue((uint32_t)(value & 0xffffffff));
                    }
                }
                properties.add("VALUE", value, SymbolTable::ST_REPLACE);
            }
            else {
                snprintf(buf, 100, "%s: could not set value to %s", _name.c_str(),
                         new_value.asString().c_str());
                MessageLog::instance()->add(buf);
                NB_MSG << buf << "\n";
            }
        }
        if (state_machine->token_id == ClockworkToken::MQTTPUBLISHER && mq_interface &&
            property_val.token_id == ClockworkToken::tokMessage) {
            //std::string old_val(properties.lookup(property.c_str()).asString());
            mq_interface->publish(properties.lookup("topic").asString(), new_value.asString(),
                                  this);
        }

        std::string property_name(modbusName(property, property_val));
        if (published) {
            Channel::sendPropertyChange(this, property.c_str(), new_value, authority);

            // update modbus with the new value
            if (modbus_exports.count(property_name)) {
                Channel::sendModbusUpdate(this, property_name, new_value);
            }
        }
        {
            Message changed_msg("PROPERTY_CHANGE");
            if (receives_functions.count(changed_msg) && !hasPending(changed_msg)) {
                enqueue(Package(this, this, changed_msg));
            }
        }
        // only tell dependent machines to recheck predicates if the property
        // actually changes value
        if (property_val.token_id != ClockworkToken::TRACE &&
            property_val.token_id != ClockworkToken::DEBUG) {
            setNeedsCheck();
            notifyDependents();
        }
        return true;
    }
}

void MachineInstance::refreshModbus(cJSON *json_array) {

    std::map<int, std::string>::iterator iter = modbus_addresses.begin();
    while (iter != modbus_addresses.end()) {
        std::string full_name = (*iter).second;
        std::string short_name(full_name);
        if (short_name.rfind('.') != std::string::npos) {
            short_name = short_name.substr(short_name.rfind('.') + 1);
        }
        int addr = (*iter).first & 0xffff;
        int group = (*iter).first >> 16;
        ModbusAddress info = modbus_exports[(*iter).second];
        if ((int)info.getGroup() != group) {
            NB_MSG << full_name << " index (" << group << "," << addr << ")" << (*iter).first
                   << " should be " << (*iter).second << "\n";
        }
        if ((int)info.getAddress() != addr) {
            NB_MSG << full_name << " index (" << group << "," << addr << ")" << (*iter).first
                   << " should be " << (*iter).second << "\n";
        }
        int length = info.length();
        if (info.kind() == ModbusExport::float32) {
            length = 2;
        }
        Value value(0);
        switch (info.getSource()) {
        case ModbusAddress::machine:
            if (_type == "VARIABLE" || _type == "CONSTANT" ||
                (properties.exists("VALUE") && (group == 3 || group == 4))) {
                value = properties.lookup("VALUE");
            }
            else if (current_state.getName() == "on" || current_state.getName() == "true") {
                value = 1;
            }
            else {
                value = 0;
            }
            break;
        case ModbusAddress::state:
            value = (_name + "." + current_state.getName() == (*iter).second) ? 1 : 0;
            break;
        case ModbusAddress::command:
            value = 0;
            break;
        case ModbusAddress::property:
            if (properties.exists(short_name.c_str())) {
                value = properties.lookup(short_name.c_str());
            }
            else {
                MachineInstance *mi = lookup((*iter).second);
                if (mi && (mi->_type == "VARIABLE" || mi->_type == "CONSTANT" ||
                           (mi->properties.exists("VALUE") && (group == 3 || group == 4)))) {
                    value = mi->getValue("VALUE");
                }
                else {
                    std::stringstream ss;
                    ss << "Error: " << full_name << " has not been initialised";
                    char *msg = strdup(ss.str().c_str());
                    MessageLog::instance()->add(msg);
                    NB_MSG << msg << "\n";
                    free(msg);
                }
            }
            break;
        case ModbusAddress::unknown:
            value = "UNKNOWN";
        }
        cJSON *item = cJSON_CreateArray();
        cJSON_AddItemToArray(item, cJSON_CreateLong(group));
        cJSON_AddItemToArray(item, cJSON_CreateLong(addr));
        cJSON_AddItemToArray(item, cJSON_CreateLong(info.kind()));
        if (owner) {
            std::string name(owner->getName());
            name += ".";
            name += full_name;
            size_t found = name.rfind(".");
            if (group == 0 && found != std::string::npos) {
                name.replace(found, 1, ".cmd_");
            }
            cJSON_AddItemToArray(item, cJSON_CreateString(name.c_str()));
        }
        else {
            std::string name(full_name);
            size_t found = name.rfind(".");
            if (group == 0 && found != std::string::npos) {
                name.replace(found, 1, ".cmd_");
            }
            cJSON_AddItemToArray(item, cJSON_CreateString(name.c_str()));
            //out << group << " " << addr << " " << full_name << " " << length << " " << value <<"\n";
        }
        cJSON_AddItemToArray(item, cJSON_CreateLong(length));
        if (value.kind == Value::t_string || value.kind == Value::t_symbol) {
            cJSON_AddItemToArray(item, cJSON_CreateString(value.sValue.c_str()));
        }
        else if (value.kind == Value::t_float) {
            cJSON_AddItemToArray(item, cJSON_CreateDouble(value.fValue));
        }
        else {
            cJSON_AddItemToArray(item, cJSON_CreateLong(value.iValue));
        }
        cJSON_AddItemToArray(json_array, item);
        iter++;
    }
}
void MachineInstance::exportModbusMapping(std::ostream &out) {

    std::map<int, std::string>::iterator iter = modbus_addresses.begin();
    while (iter != modbus_addresses.end()) {
        int addr = (*iter).first & 0xffff;
        int group = (*iter).first >> 16;
        ModbusAddress info = modbus_exports[(*iter).second];
        //assert((int)info.getGroup() == group);
        //assert(info.getAddress() == addr);
        const char *data_type;
        if (info.kind() == ModbusExport::float32) {
            data_type = "Floating_PT_32";
        }
        else {
            switch (ModbusAddress::toGroup(group)) {
            case ModbusAddress::discrete:
                data_type = "Discrete";
                break;
            case ModbusAddress::coil:
                data_type = "Discrete";
                break;
            case ModbusAddress::input_register:
            case ModbusAddress::holding_register:
                if (info.length() == 1) {
                    data_type = "Signed_int_16";
                }
                else if (info.length() == 2) {
                    data_type = "Signed_int_32";
                }
                else {
                    data_type = "Ascii_String";
                }
                break;
            default:
                data_type = "Unknown";
            }
        }
        if (owner)
            out << group << ":" << std::setfill('0') << std::setw(5) << addr << "\t"
                << owner->getName() << "." << ((group == 0) ? "cmd_" : "") << (*iter).second << "\t"
                << data_type << "\t" << info.length() << "\n";
        else {
            std::string name((*iter).second);
            size_t found = name.rfind(".");
            if (group == 0 && found != std::string::npos) {
                name.replace(found, 1, ".cmd_");
            }
            out << group << ":" << std::setfill('0') << std::setw(5) << addr << "\t" << name << "\t"
                << data_type << "\t" << info.length() << "\n";
        }
        iter++;
    }
}

// Modbus

ModbusAddress MachineInstance::addModbusExport(std::string name, ModbusAddress::Group g,
                                               unsigned int n, ModbusAddressable *owner,
                                               ModbusExport::Type kind, ModbusAddress::Source src,
                                               const std::string &full_name) {
    if (ModbusAddress::preset_modbus_mapping.count(full_name) != 0) {
        ModbusAddressDetails info = ModbusAddress::preset_modbus_mapping[full_name];
        DBG_MODBUS << _name << " " << name << " has predefined address: " << info.group << ":"
                   << std::setfill('0') << std::setw(5) << info.address << "\n";
        ModbusAddress addr(ModbusAddress::toGroup(info.group), info.address, info.len, owner, src,
                           full_name, kind);
        modbus_exports[name] = addr;
        return addr;
    }
    else {
        ModbusAddress addr = ModbusAddress::alloc(g, n, this, src, full_name, kind);
        DBG_MODBUS << _name << " " << name << " allocated new address " << addr << "\n";
        modbus_exports[name] = addr;
        return addr;
    }
}

void MachineInstance::setupModbusPropertyExports(std::string property_name,
                                                 ModbusAddress::Group grp, ModbusExport::Type kind,
                                                 int size) {
    std::string full_name;
    if (owner) {
        full_name = owner->getName() + ".";
    }
    std::string name;
    if (_name != property_name) {
        name = _name + ".";
    }
    name += property_name;
    switch (grp) {
    case ModbusAddress::discrete:
        addModbusExport(name, ModbusAddress::discrete, size, this, kind, ModbusAddress::property,
                        full_name + name);
        break;
    case ModbusAddress::coil:
        addModbusExport(name, ModbusAddress::coil, size, this, kind, ModbusAddress::property,
                        full_name + name);
        break;
    case ModbusAddress::input_register:
        addModbusExport(name, ModbusAddress::input_register, size, this, kind,
                        ModbusAddress::property, full_name + name);
        break;
    case ModbusAddress::holding_register:
        addModbusExport(name, ModbusAddress::holding_register, size, this, kind,
                        ModbusAddress::property, full_name + name);
        break;
    case ModbusAddress::string:
        addModbusExport(name, ModbusAddress::input_register, size, this, kind,
                        ModbusAddress::property, full_name + name);
        break;
    default:;
    }
}

void MachineInstance::setupModbusInterface() {

    if (modbus_addresses.size() != 0) {
        return; // already done
    }
    if (isPrivateConstant()) {
        // A private constant may be consumed by Clockwork expressions and plugins,
        // but it must never be assigned an externally readable Modbus address.
        if (properties.exists("export") || !state_machine->exports.empty() ||
            !state_machine->state_exports.empty() || !state_machine->state_exports_rw.empty() ||
            !state_machine->command_exports.empty()) {
            MessageLog::instance()->add((_name + " is private; ignoring Modbus exports").c_str());
        }
        return;
    }
    DBG_MODBUS << fullName() << " setting up modbus\n";
    std::string full_name = fullName();

    bool self_discrete = false;
    bool self_coil = false;
    bool self_reg = false;
    bool self_rwreg = false;
    int64_t str_length = 0;

    // workout the export type for this machine
    ModbusExport::Type export_type = ModbusExport::none;

    bool exported = properties.exists("export");
    if (exported) {
        Value export_type_val = properties.lookup("export");
        if (export_type_val != "false") {
            if (_type == "POINT") {
                const Value &type = properties.lookup("type");
                if (type != SymbolTable::Null) {
                    if (type == "Input") {
                        self_discrete = true;
                        export_type = ModbusExport::discrete;
                    }
                    else if (type == "Output") {
                        if (export_type_val == "rw") {
                            self_coil = true;
                            export_type = ModbusExport::coil;
                        }
                        else {
                            self_discrete = true;
                            export_type = ModbusExport::coil;
                        }
                    }
                    else if (type == "AnalogueInput") {
                        self_reg = true;
                        export_type = ModbusExport::reg;
                    }
                    else if (type == "AnalogueOutput") {
                        // default to readonly export
                        if (export_type_val == "rw") {
                            self_rwreg = true;
                            export_type = ModbusExport::rw_reg;
                        }
                        else {
                            self_reg = true;
                            export_type = ModbusExport::reg;
                        }
                    }
                }
            }
            else if (export_type_val == "rw") {
                self_coil = true;
                export_type = ModbusExport::coil;
            }
            else if (export_type_val == "reg") {
                self_reg = true;
                export_type = ModbusExport::reg;
            }
            else if (export_type_val == "rw_reg") {
                self_rwreg = true;
                export_type = ModbusExport::rw_reg;
            }
            else if (export_type_val == "reg32") {
                self_reg = true;
                export_type = ModbusExport::reg32;
            }
            else if (export_type_val == "rw_reg32") {
                self_rwreg = true;
                export_type = ModbusExport::rw_reg32;
            }
            else if (export_type_val == "float32") {
                self_rwreg = true;
                export_type = ModbusExport::float32;
            }
            else if (export_type_val == "str") {
                const Value &export_size = properties.lookup("strlen");
                self_reg = true;
                export_type = ModbusExport::str;
                export_size.asInteger(str_length);
            }
            else if (export_type_val == "rw_str") {
                const Value &export_size = properties.lookup("strlen");
                self_rwreg = true;
                export_type = ModbusExport::str;
                export_size.asInteger(str_length);
            }
            else {
                self_discrete = true;
                export_type = ModbusExport::discrete;
            }
        }
    }
    if (_type != "VARIABLE" &&
        _type != "CONSTANT") { // export property refers to VALUE export type, not the machine
        modbus_exported = export_type;
    }

    // register the individual properties

    if (_type != "VARIABLE" && _type != "CONSTANT") {
        // exporting 'self' (either 'on' state or 'VALUE' property depending on export type)
        if (self_coil) {
            modbus_address = addModbusExport(_name, ModbusAddress::coil, 1, this, export_type,
                                             ModbusAddress::machine, full_name);
        }
        else if (self_discrete) {
            modbus_address = addModbusExport(_name, ModbusAddress::discrete, 1, this, export_type,
                                             ModbusAddress::machine, full_name);
            //addModbusExportname(_name].setName(full_name);
        }
        else if (self_reg) {
            int len = 1;
            if (export_type == ModbusExport::reg) {
                len = 1;
            }
            else if (export_type == ModbusExport::reg32) {
                len = 2;
            }
            else if (export_type == ModbusExport::str && str_length == 0) {
                len = 80;
            }
            modbus_address = addModbusExport(_name, ModbusAddress::input_register, len, this,
                                             export_type, ModbusAddress::machine, full_name);
            //modbus_exports[_name].setName(full_name);
        }
        else if (self_rwreg) {
            int len = 1;
            if (export_type == ModbusExport::rw_reg32) {
                len = 2;
            }
            else if (export_type == ModbusExport::str && str_length == 0) {
                len = 80;
            }
            addModbusExport(_name, ModbusAddress::holding_register, len, this, export_type,
                            ModbusAddress::machine, full_name);
            //modbus_exports[name(_name].setName(full_name);
        }
    }

    // exported states
    BOOST_FOREACH (std::string s, state_machine->state_exports) {
        std::string name = _name + "." + s;
        addModbusExport(name, ModbusAddress::discrete, 1, this, ModbusExport::discrete,
                        ModbusAddress::state, full_name + "." + s);
        //modbus_exports[name(name].setName(full_name+"."+s);
    }
    // exported states
    BOOST_FOREACH (std::string s, state_machine->state_exports_rw) {
        std::string name = _name + "." + s;
        addModbusExport(name, ModbusAddress::coil, 1, this, ModbusExport::discrete,
                        ModbusAddress::state, full_name + "." + s);
        //modbus_exports[name(name].setName(full_name+"."+s);
    }

    // exported commands
    BOOST_FOREACH (std::string s, state_machine->command_exports) {
        std::string name = _name + "." + s;
        addModbusExport(name, ModbusAddress::coil, 1, this, ModbusExport::discrete,
                        ModbusAddress::command, full_name + "." + s);
        //addModbusExportname(name].setName(full_name+"."+s);
    }

    if (_type == "VARIABLE" || _type == "CONSTANT") {
        std::string property_name(_name);
        switch (export_type) {
        case ModbusExport::none:
            break;
        case ModbusExport::discrete:
            setupModbusPropertyExports(property_name, ModbusAddress::discrete,
                                       ModbusExport::discrete, 1);
            break;
        case ModbusExport::coil:
            setupModbusPropertyExports(property_name, ModbusAddress::coil, ModbusExport::coil, 1);
            break;
        case ModbusExport::reg:
            setupModbusPropertyExports(property_name, ModbusAddress::input_register,
                                       ModbusExport::reg, 1);
            break;
        case ModbusExport::rw_reg:
            setupModbusPropertyExports(property_name, ModbusAddress::holding_register,
                                       ModbusExport::rw_reg, 1);
            break;
        case ModbusExport::reg32:
            setupModbusPropertyExports(property_name, ModbusAddress::input_register,
                                       ModbusExport::reg32, 2);
            break;
        case ModbusExport::rw_reg32:
            setupModbusPropertyExports(property_name, ModbusAddress::holding_register,
                                       ModbusExport::rw_reg32, 2);
            break;
        case ModbusExport::float32:
            setupModbusPropertyExports(property_name, ModbusAddress::holding_register,
                                       ModbusExport::float32, 2);
            break;
        case ModbusExport::str:
            if (self_reg) {
                setupModbusPropertyExports(property_name, ModbusAddress::input_register,
                                           ModbusExport::str, (int)str_length);
            }
            else {
                setupModbusPropertyExports(property_name, ModbusAddress::holding_register,
                                           ModbusExport::str, (int)str_length);
            }
            break;
        }
    }

    BOOST_FOREACH (ModbusAddressTemplate mat, state_machine->exports) {
        //if (export_type == none) continue;
        setupModbusPropertyExports(mat.property_name, mat.grp, mat.exType, mat.size);
    }

    assert(modbus_addresses.size() == 0);
    std::map<std::string, ModbusAddress>::iterator iter = modbus_exports.begin();
    while (iter != modbus_exports.end()) {
        const ModbusAddress &ma = (*iter).second;
        int index = ((int)ma.getGroup() << 16) + ma.getAddress();
        if (modbus_addresses.count(index) != 0) {
            NB_MSG << _name << " " << (*iter).first << " address " << ma << " already recorded as "
                   << modbus_addresses[index] << "\n";
        }
        assert(modbus_addresses.count(index) == 0);
        modbus_addresses[index] = (*iter).first;
        iter++;
    }
    if (modbus_addresses.size() != modbus_exports.size()) {
        NB_MSG << _name << " modbus export inconsistent. addresses: " << modbus_addresses.size()
               << " indexes " << modbus_exports.size() << "\n";
    }
    assert(modbus_addresses.size() == modbus_exports.size());
}

void MachineInstance::modbusUpdated(ModbusAddress &base_addr, unsigned int offset,
                                    const char *new_value, Value::Kind kind) {
    std::string name = fullName();
    DBG_MODBUS << name << " modbusUpdated " << base_addr << " " << offset << " " << new_value
               << "\n";
    int index = (base_addr.getGroup() << 16) + base_addr.getAddress() + offset;
    if (!modbus_addresses.count(index)) {
        std::stringstream ss;
        ss << name << " Error: bad modbus address lookup for " << base_addr;
        MessageLog::instance()->add(ss.str().c_str());
        return;
    }
    std::string item_name = modbus_addresses[index];
    if (!modbus_exports.count(item_name)) {
        std::stringstream ss;
        ss << name << " Error: bad modbus name lookup for " << item_name;
        MessageLog::instance()->add(ss.str().c_str());
        return;
    }
    ModbusAddress addr = modbus_exports[item_name];
    DBG_MODBUS << name << " local ModbusAddress found: " << addr << "\n";

    if (addr.getGroup() == ModbusAddress::holding_register) {
        DBG_MODBUS << name << " holding register update\n";
        std::string property_name = modbus_addresses[index];
        DBG_MODBUS << _name << " set property " << property_name << " (" << kind << ") via modbus index " << index
                   << " (" << addr << ")\n";
        if (property_name == _name) {
            setValue("VALUE", Value(new_value, kind));
        }
        else {
            setValue(property_name, Value(new_value, kind));
        }
    }
    else {
        NB_MSG << name << " unexpected modbus group for write operation " << addr << "\n";
    }
}

void MachineInstance::modbusUpdated(ModbusAddress &base_addr, unsigned int offset, int new_value) {
    std::string name(fullName());
    DBG_MODBUS << name << " modbusUpdated " << base_addr << " " << offset << " " << new_value
               << "\n";
    int index = (base_addr.getGroup() << 16) + base_addr.getAddress() + offset;
    if (!modbus_addresses.count(index)) {
        NB_MSG << name << " Error: bad modbus address lookup for " << base_addr << "\n";
        return;
    }
    const std::string &item_name = modbus_addresses[index];
    if (!modbus_exports.count(item_name)) {
        NB_MSG << name << " Error: bad modbus name lookup for " << item_name << "\n";
        return;
    }
    ModbusAddress addr = modbus_exports[item_name];
    DBG_MODBUS << name << " local ModbusAddress found: " << addr << "\n";

    if (addr.getGroup() == ModbusAddress::coil || addr.getGroup() == ModbusAddress::discrete) {

        if (addr.getSource() == ModbusAddress::machine) {
            // crosscheck - the machine must have been exported read/write
            if (!properties.exists("export")) {
                char buf[100];
                snprintf(
                    buf, 100,
                    "received a modbus update for machine %s but that machine has no export property",
                    name.c_str());
                MessageLog::instance()->add(buf);
            }

            DBG_MODBUS << "setting state of " << name << " due to modbus command\n";
            if (new_value) {
                SetStateActionTemplate ssat(CStringHolder("SELF"), "on");
                enqueueAction(ssat.factory(
                    this)); // execute this state change once all other actions are complete
            }
            else {
                SetStateActionTemplate ssat(CStringHolder("SELF"), "off");
                enqueueAction(ssat.factory(
                    this)); // execute this state change once all other actions are complete
            }
            return;
        }
        else if (addr.getSource() == ModbusAddress::state) {
            std::string state_name = addr.getName();
            size_t found = state_name.find(".");
            if (found != std::string::npos) {
                std::string state = state_name.substr(found + 1);
                std::string name = state_name.erase(found);
                if (_name != name) {
                    char buf[100];
                    snprintf(
                        buf, 100,
                        "%s: Warning: modbus object name %s does not match the machine receiving the event %s",
                        _name.c_str(), name.c_str(), state_name.c_str());
                    MessageLog::instance()->add(buf);
                }
                SetStateActionTemplate ssat(CStringHolder("SELF"), state);
                enqueueAction(ssat.factory(
                    this)); // execute this state change once all other actions are complete
            }
        }
        else if (addr.getSource() == ModbusAddress::command) {
            if (new_value) {
                std::string &cmd_name = modbus_addresses[index];
                DBG_MODBUS << _name << " executing command " << cmd_name << "\n";
                // execute this command once all other actions are complete
                handle(Message(cmd_name.c_str()), this,
                       true); // fire the trigger when the command is done
            }
            return;
        }
        else if (addr.getSource() == ModbusAddress::property) {
            std::string property_name = modbus_addresses[index];
            DBG_MODBUS << _name << " set property " << property_name << " via modbus index "
                       << index << " (" << addr << ")\n";
            if (name == _name) {
                setValue("VALUE", new_value);
            }
            else {
                setValue(property_name, new_value);
            }
        }
        else {
            DBG_MODBUS << _name << " received update for out-of-range coil" << offset << "\n";
        }
    }
    else if (addr.getGroup() == ModbusAddress::holding_register) {
        DBG_MODBUS << name << " holding register update\n";
        std::string property_name = modbus_addresses[index];
        DBG_MODBUS << _name << " set property " << property_name << " via modbus index " << index
                   << " (" << addr << ")\n";
        if (property_name == _name) {
            setValue("VALUE", new_value);
        }
        else {
            setValue(property_name, new_value);
        }
    }
    else {
        NB_MSG << name << " unexpected modbus group for write operation " << addr << "\n";
    }
}

void MachineInstance::modbusUpdated(ModbusAddress &base_addr, unsigned int offset,
                                    float new_value) {
    std::string name(fullName());
    DBG_MODBUS << name << " modbusUpdated " << base_addr << " " << offset << " " << new_value
               << "\n";
    int index = (base_addr.getGroup() << 16) + base_addr.getAddress() + offset;
    if (!modbus_addresses.count(index)) {
        NB_MSG << name << " Error: bad modbus address lookup for " << base_addr << "\n";
        return;
    }
    const std::string &item_name = modbus_addresses[index];
    if (!modbus_exports.count(item_name)) {
        NB_MSG << name << " Error: bad modbus name lookup for " << item_name << "\n";
        return;
    }
    ModbusAddress addr = modbus_exports[item_name];
    DBG_MODBUS << name << " local ModbusAddress found: " << addr << "\n";

    if (addr.getGroup() == ModbusAddress::holding_register) {
        DBG_MODBUS << name << " holding register update\n";
        std::string property_name = modbus_addresses[index];
        DBG_MODBUS << _name << " set property " << property_name << " via modbus index " << index
                   << " (" << addr << ")\n";
        if (property_name == _name) {
            setValue("VALUE", new_value);
        }
        else {
            setValue(property_name, new_value);
        }
    }
    else {
        NB_MSG << name << " unexpected modbus group " << addr.getGroup()
               << " for write operation (float) " << addr << "\n";
    }
}

int MachineInstance::getModbusValue(ModbusAddress &addr, unsigned int offset, int len) {
    int pos = (addr.getGroup() << 16) + addr.getAddress() + offset;
    if (!modbus_addresses.count(pos)) {
        DBG_M_MODBUS << _name << " unknown modbus address offset requested " << offset << "\n";
        return 0;
    }
    // self
    std::string devname = modbus_addresses[pos];
    DBG_M_MODBUS << _name << " found device " << devname << " matching address " << addr.getGroup()
                 << ":" << addr.getAddress() + offset << "\n";
    if (devname == _name && addr.getAddress() == 0) {
        if (_type != "VARIABLE" && _type != "CONSTANT") {
            if (current_state.getName() == "on") {
                return 1;
            }
            else {
                return 0;
            }
        }
        else {
            const Value &v = getValue("VALUE");
            int64_t intVal;
            if (v.asInteger(intVal)) {
                return (int)intVal;
            }
            else {
                return 0;
            }
        }
    }
    return 0;
}

// Error states (not used yet)
bool MachineInstance::inError() { return error_state != 0; }

void MachineInstance::setError(int val) {
    if (error_state) {
        return;
    }
    error_state = val;
    if (val) {
        saved_state = current_state;
        const State *err = state_machine->findState("ERROR");
        assert(err);
        setState(*err);
    }
}

void MachineInstance::resetError() {
    error_state = 0;
    if (state_machine) {
        setState(state_machine->initial_state);
    }
}

void MachineInstance::ignoreError() { error_state = 0; }
