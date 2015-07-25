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

#include <dlfcn.h>
#include <cassert>
#include <iostream>
#include <algorithm>
#include <sys/time.h>
#include <time.h>
#include <utility>
#include <vector>
#include "Plugin.h"
#include "MachineInstance.h"
#include "State.h"
#include "Dispatcher.h"
#include "Message.h"
#include "IOComponent.h"
#include "Logger.h"
#include "MessagingInterface.h"
#include "DebugExtra.h"
#include "Scheduler.h"
#include "IfCommandAction.h"
#include "Expression.h"
#include "FireTriggerAction.h"
#include "HandleMessageAction.h"
#include "ExecuteMessageAction.h"
#include "CallMethodAction.h"
#include "dynamic_value.h"
#include "options.h"
#include "MessageLog.h"
#include "cJSON.h"
#include "Channel.h"

extern int num_errors;
extern std::list<std::string>error_messages;

static uint64_t process_time = 0; // used for idle calculations for rate estimation
Value *MachineInstance::polling_delay = 0;

unsigned int MachineInstance::num_machines_with_work = 1; // machine idle processing will occur if this value is non zero
unsigned int MachineInstance::total_machines_needing_check = 1;

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

  Cache() : full_name(0), modbus_name(0), reported_error(false) { }
};

Parameter::Parameter(Value v) : val(v), machine(0) {
	;
}
Parameter::Parameter(const char *name, const SymbolTable &st) : val(name), properties(st), machine(0) { }
std::ostream &Parameter::operator<< (std::ostream &out)const {
	return out << val << "(" << properties << ")";
}
Parameter::Parameter(const Parameter &orig) {
	val = orig.val; machine = orig.machine; properties = orig.properties;
}

Transition::Transition(State s, State d, Message t, Predicate *p) : source(s), dest(d), trigger(t), condition(0) {
	if (p) {
		condition = new Condition(p);
	}
}

Transition::Transition(const Transition &other) : source(other.source), dest(other.dest), trigger(other.trigger), condition(0) {
	if (other.condition && other.condition->predicate) {
		condition = new Condition(new Predicate(*other.condition->predicate));
	}
}

Transition::~Transition() {
	delete condition;
}

Transition &Transition::operator=(const Transition &other) {
	source = other.source;
	dest = other.dest;
	trigger = other.trigger;
	if (other.condition) {
		delete condition;
		condition = new Condition(other.condition->predicate);
	}
	return *this;
}

	ConditionHandler::ConditionHandler(const ConditionHandler &other)
: condition(other.condition),
	command_name(other.command_name),
	flag_name(other.flag_name),
	timer_val(other.timer_val),
	action(other.action),
	trigger(other.trigger),
	uses_timer(other.uses_timer),
	triggered(other.triggered)
{
	if (trigger) trigger->retain();
}

ConditionHandler &ConditionHandler::operator=(const ConditionHandler &other) {
	condition = other.condition;
	command_name = other.command_name;
	flag_name = other.flag_name;
	timer_val = other.timer_val;
	action = other.action;
	trigger = other.trigger;
	if (trigger) trigger->retain();
	uses_timer = other.uses_timer;
	triggered = other.triggered;
	return *this;
}

bool ConditionHandler::check(MachineInstance *machine) {
	if (!machine) return false;
	if (command_name == "FLAG" ) {
		MachineInstance *flag = machine->lookup(flag_name);
		if (!flag)
			std::cerr << machine->getName() << " error: flag " << flag_name << " not found\n";
		else {
			if (condition(machine)) {
				const State *on = flag->state_machine->findState("on");
				if (!flag->isActive()) flag->setState(*on);
				else {
					// execute this state change once all other actions are complete
					SetStateActionTemplate ssat("SELF", "on" );
					flag->enqueueAction(ssat.factory(flag));
				}
			}
			else {
				const State *off = flag->state_machine->findState("off");
				if (!flag->isActive()) flag->setState(*off);
				else {
					// execute this state change once all other actions are complete
					SetStateActionTemplate ssat("SELF", "off" );
					flag->enqueueAction(ssat.factory(flag));
				}
			}
		}
	}
	else if (!triggered && ( (trigger && trigger->fired()) || !trigger) && condition(machine)) {
		triggered = true;
		DBG_AUTOSTATES << machine->getName() << " subcondition triggered: " << command_name << " " << *(condition.predicate) << "\n";
		machine->execute(Message(command_name.c_str()),machine);
		if (trigger) trigger->disable();
	}
	else {
		DBG_AUTOSTATES <<"condition: " << (*condition.predicate) << "\n";
		if (!trigger) { DBG_AUTOSTATES << "    condition does not have a timer\n"; }
		if (triggered) {DBG_AUTOSTATES <<"     condition " << (condition.predicate) << " already triggered\n";}
		if (condition(machine)) {DBG_AUTOSTATES <<"    condition " << (condition.predicate) << " passes\n";}
	}
	if (triggered) return true;
	return false;
}

void ConditionHandler::reset() {
	if (trigger) {
		//NB_MSG << "clearing trigger on subcondition of state " << s.state_name << "\n";
		if (trigger->enabled())
			trigger->disable();
		trigger->release();
	}
	trigger = 0;
	triggered = false;
}


std::ostream &operator<<(std::ostream &out, const Parameter &p) {
	return p.operator<<(out);
}

std::ostream &operator<<(std::ostream &out, const ActionTemplate &a) {
	return a.operator<<(out);
}

std::ostream &operator<<(std::ostream &out, const StableState &ss) {
	return ss.operator<<(out);
}

StableState::~StableState() {
	if (trigger) {
		if (trigger->enabled()) {
			trigger->disable();
			trigger->release();
		}
	}
	if (subcondition_handlers) {
		subcondition_handlers->clear();
		delete subcondition_handlers;
	}
}

	StableState::StableState (const StableState &other)
: state_name(other.state_name), condition(other.condition),
	uses_timer(other.uses_timer), timer_val(0), trigger(0), subcondition_handlers(0), owner(other.owner)
{
	if (other.subcondition_handlers) {
		subcondition_handlers = new std::list<ConditionHandler>;
		std::copy(other.subcondition_handlers->begin(), other.subcondition_handlers->end(), back_inserter(*subcondition_handlers));
	}
}


void StableState::triggerFired(Trigger *trig) {
	if (owner) owner->setNeedsCheck();
}

#if 0
// TBD This method isn't used in the current code
void StableState::refreshTimer() {
	// prepare a new trigger. note: very short timers will still be scheduled
	trigger = new Trigger("SSRefreshTimer");
	long trigger_time;

	// BUG here. If the timer comparison is '>' (ie Timer should be > the given value
	//   we should trigger at v.iValue+1
	if (timer_val.kind == Value::t_symbol) {
		Value v = owner->getValue(timer_val.sValue);
		if (v.kind != Value::t_integer) {
			NB_MSG << owner->getName() << " Error: timer value for state " << state_name << " is not numeric\n";
			NB_MSG << "timer_val: " << timer_val << " lookup value: " << v << " type: " << v.kind << "\n";
			return;
		}
		else
			trigger_time = v.iValue;
	}
	else if (timer_val.kind == Value::t_integer)
		trigger_time = timer_val.iValue;
	else {
		DBG_SCHEDULER << owner->getName() << " Warning: timer value for state " << state_name << " is not numeric\n";
		NB_MSG << "timer_val: " << timer_val << " type: " << timer_val.kind << "\n";
		return;
	}
	DBG_SCHEDULER << owner->getName() << " Scheduling timer for " << timer_val*1000 << "us\n";
	Scheduler::instance()->add(new ScheduledItem(trigger_time*1000, new FireTriggerAction(owner, trigger)));
}
#endif
std::map<std::string, MachineInstance*> machines;
std::map<std::string, MachineClass*> machine_classes;

// All machine instances automatically join and leave this list. 
// During the poll process, all machines in this list have their idle() called.
std::list<MachineInstance*> MachineInstance::all_machines;
std::list<MachineInstance*> MachineInstance::automatic_machines;
std::list<MachineInstance*> MachineInstance::active_machines;
std::list<MachineInstance*> MachineInstance::shadow_machines;
std::set<MachineInstance*> MachineInstance::busy_machines;
std::set<MachineInstance*> MachineInstance::plugin_machines;
std::list<Package*> MachineInstance::pending_events;
std::set<MachineInstance*> MachineInstance::pending_state_change;
std::map<std::string, MachineInterface *> MachineInterface::all_interfaces;
std::map<std::string, HardwareAddress> MachineInstance::hw_names;


std::list<MachineClass*> MachineClass::all_machine_classes;

std::map<std::string, MachineClass> MachineClass::machine_classes;

/* Factory methods */

MachineInstance *MachineInstanceFactory::create(CStringHolder name, const char * type, MachineInstance::InstanceType instance_type) {
	if (instance_type == MachineInstance::MACHINE_SHADOW)
		return new MachineShadowInstance(name, type, MachineInstance::MACHINE_INSTANCE);
	if (strcmp(type, "COUNTERRATE") == 0)
		return new CounterRateInstance(name, type, instance_type);
	else if (strcmp(type, "RATEESTIMATOR") == 0)
		return new RateEstimatorInstance(name, type, instance_type);
	else {
		MachineClass *cls = MachineClass::find(type);
		ChannelDefinition *defn = dynamic_cast<ChannelDefinition*>(cls);
		if (defn) {
			return new Channel(name.get(), type);
		}
		return new MachineInstance(name, type, instance_type);
	}
}

void MachineInstance::setNeedsCheck() {
	//DBG_MSG << _name << "::setNeedsCheck(), enabled: " << is_enabled 
	//	<< " has state machine? " << ( (state_machine) ? "yes" : "no") << "\n";
	if (!getStateMachine() ) return;
	if (!is_enabled) return;
	if (!needs_check) { 
		++needs_check;  DBG_AUTOSTATES << _name << " needs check\n"; 
		++total_machines_needing_check;
	}
	if (!active_actions.empty() || !mail_queue.empty()) {
		//DBG_MSG << _name << " queued for action processing\n";
		busy_machines.insert(this);
	}
	else if (getStateMachine()->allow_auto_states) {
		//DBG_MSG << _name << " queued for stable state checks\n";
		pending_state_change.insert(this);
	}
	else {
		//DBG_MSG << _name << " queued for action processing\n";
		busy_machines.insert(this);
	}
	next_poll = 0;
	if (state_machine->token_id == ClockworkToken::LIST) {
		std::set<MachineInstance*>::iterator dep_iter = depends.begin();
		while (dep_iter != depends.end()) {
			MachineInstance *dep = *dep_iter++;
			if (dep->is_enabled) dep->setNeedsCheck();
		}
	}
	else if (state_machine->token_id == ClockworkToken::REFERENCE) {
		std::set<MachineInstance*>::iterator dep_iter = depends.begin();
		while (dep_iter != depends.end()) {
			MachineInstance *dep = *dep_iter++;
			if (dep->is_enabled) dep->setNeedsCheck();
		}
	}
#if 0
	if (io_interface) {
		DBG_MSG << _name << " has hw io, updating dependent machines\n";
		std::set<MachineInstance*>::iterator dep_iter = depends.begin();
		while (dep_iter != depends.end()) {
			MachineInstance *dep = *dep_iter++;
			if (dep->is_enabled) dep->setNeedsCheck();
		}
	}
#endif
}

std::string &MachineInstance::fullName() const {
	if (cache->full_name) return *cache->full_name;
	std::string res = _name;
	MachineInstance *o = owner;
	while (o) {
		res = o->getName() + "." + res;
		o = o->owner;
	}
	cache->full_name = new std::string(res);
	return *cache->full_name;
}

std::string MachineInstance::modbusName(const std::string &property, const Value & property_val) {
	std::string res;
	if (cache->modbus_name) res = *cache->modbus_name;
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
	return false;
}

void MachineInstance::enqueueAction(Action *a){
	if (a) {
		active_actions.push_front(a);
	}
	num_machines_with_work++;
	DBG_ACTIONS << _name << " New Action queued: " << *a << "\n";
	has_work = true;
	busy_machines.insert(this);
}

void MachineInstance::enqueue(const Package &package) {
	Receiver::enqueue(package);
	setNeedsCheck();
}

bool MachineInstance::workToDo() { 
	return !pending_events.empty() || !busy_machines.empty() || !pending_state_change.empty()
				|| num_machines_with_work + total_machines_needing_check > 0; 
}
std::set<MachineInstance*>& MachineInstance::busyMachines() { return busy_machines; }
std::list<Package*>& MachineInstance::pendingEvents() { return pending_events; }
std::set<MachineInstance*>& MachineInstance::pluginMachines() { return plugin_machines; }

void MachineInstance::forceIdleCheck() { 
	num_machines_with_work++; 
	DBG_AUTOSTATES  << " forced an idle check " << num_machines_with_work << "\n";
}


#if 0
void MachineInstance::add_io_entry(const char *name, unsigned int io_offset, unsigned int bit_offset){
	HardwareAddress addr(io_offset, bit_offset);
	addr.io_offset = io_offset;
	addr.io_bitpos = bit_offset;
	hw_names[name] = addr;
}
#endif

bool TriggeredAction::active() {
	std::set<IOComponent*>::iterator iter = trigger_on.begin();
	while (iter != trigger_on.end()) {
		IOComponent *ioc = *iter++;
		if (!ioc->isOn())
			return false; 
	}
	iter = trigger_off.begin();
	while (iter != trigger_off.end()) {
		IOComponent *ioc = *iter++;
		if (!ioc->isOff())
			return false; 
	}
	return true;
}


void MachineInstance::triggerFired(Trigger *trig) {
	static const std::string str_publish("publish");
	if (trig->matches(str_publish)) {
		std::cout << "trigger fired, sending property changes\n";
		Channel::sendPropertyChanges(this);
	}
	setNeedsCheck();
}

void MachineInstance::checkActions() {
	num_machines_with_work++;
	has_work = true;
	busy_machines.insert(this);
	DBG_AUTOSTATES  << _name << " ADDED to machines with work " << busy_machines.size() << "\n";
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
	else
		return 0;
}

MachineInstance *MachineInstance::lookup(Value &val) {
	if (!val.cached_machine && (val.kind == Value::t_symbol || val.kind == Value::t_string)) {
		val.cached_machine = lookup(val.sValue);
	}
	return val.cached_machine;
}

bool MachineInstance::uses(MachineInstance *other) {
	if (_type == other->_type) return other->_name < _name;
	if (other->_type == "MODULE") return true;
	if (_type == "MODULE") return false;
	if (other->_type == "POINT") return true;
	if (other->_type == "ANALOGINPUT") return true;
	if (other->_type == "COUNTERRATE") return true;
	if (other->_type == "COUNTER") return true;
	if (other->_type == "COUNTERRATE") return true;
	if (other->_type == "ANALOGOUTPUT") return true;
	if (other->_type == "STATUS_FLAG") return true;
	if (_type == "POINT") return false;
	if (_type == "ANALOGINPUT") return true;
	if (other->_type == "COUNTER") return true;
	if (_type == "COUNTERRATE") return true;
	if (_type == "ANALOGOUTPUT") return true;
	if (_type == "STATUS_FLAG") return true;
	if (other->_type == "FLAG") return true;
	if (_type == "FLAG") return false;
	if (dependsOn(other)) return true;
	if (other->dependsOn(this)) return false;
	return other->_name < _name;
}

// return true if b depends on a
bool machine_dependencies( MachineInstance *a, MachineInstance *b) {
	return b->uses(a);
}

void MachineInstance::sort() {
#if 0
	std::vector<MachineInstance*> tmp(all_machines.size());
	std::copy(all_machines.begin(), all_machines.end(), tmp.begin());
	//  std::sort(tmp.begin(), tmp.end(), machine_dependencies);
	all_machines.clear();
	std::copy(tmp.begin(), tmp.end(), back_inserter(all_machines));

	DBG_PARSER << "Sorted machines (dependencies)\n";
	BOOST_FOREACH(MachineInstance *m, all_machines) {
		if (m->owner) DBG_PARSER << m->owner->getName() << ".";
		DBG_PARSER << m->getName() << "\n";
	}
#endif
}

MachineInstance *MachineInstance::lookup(const char *name) {
	std::string seek_machine_name(name);
	return lookup(seek_machine_name);
}

MachineInstance *MachineInstance::lookup(const std::string &seek_machine_name)  {
	std::map<std::string, MachineInstance *>::iterator iter = localised_names.find(seek_machine_name);
	if (iter != localised_names.end()) {
		return (*iter).second;
	}
	return lookup_cache_miss(seek_machine_name);
}

MachineInstance *MachineInstance::lookup_cache_miss(const std::string &seek_machine_name)  {

	std::string short_name(seek_machine_name);
	size_t pos = short_name.find('.');
	if (pos != std::string::npos) {
		std::string machine_name(seek_machine_name);
		machine_name.erase(pos);
		std::string extra = short_name.substr(pos+1);
		MachineInstance *mi = lookup(machine_name);
		if (mi) 
			return mi->lookup(extra);
		else
			return 0;
	}

	MachineInstance *found = 0;
	resetTemporaryStringStream();
	//ss << _name<< " cache miss lookup " << seek_machine_name << " from " << _name;
	if (seek_machine_name == "SELF" || seek_machine_name == _name) { 
		//ss << "...self";
		if (state_machine->token_id == ClockworkToken::tokCONDITION && owner)
			found = owner;
		else
			found = this; 
		goto cache_local_name;
	}
	else if (seek_machine_name == "OWNER" ) {
		if (owner) found = owner; else found = this;
		goto cache_local_name;
	}
	for (unsigned int i=0; i<locals.size(); ++i) {
		Parameter &p = locals[i];
		if (p.val.kind == Value::t_symbol && p.val.sValue == seek_machine_name) {
			//ss << "...local";
			found = p.machine; 
			goto cache_local_name;
		}
	}
	for (unsigned int i=0; i<parameters.size(); ++i) {
		Parameter &p = parameters[i];
		if (p.val.kind == Value::t_symbol &&
				(p.val.sValue == seek_machine_name || p.real_name == seek_machine_name) && p.machine) {
			//ss << "...parameter";
			found = p.machine; 
			goto cache_local_name;
		}
	}
	if (machines.count(seek_machine_name)) {
		//ss << "...global";
		found = machines[seek_machine_name];
		goto cache_local_name;
	}
	//NB_MSG << "CACHE MISS: failed to find machine " << seek_machine_name << "\n";
	//ss << " not found";
	//	DBG_M_MACHINELOOKUPS << ss << "";
	return 0;

cache_local_name:
	//	DBG_M_MACHINELOOKUPS << "\n";

	if (found) {
		if (seek_machine_name != found->getName())  {
			DBG_M_MACHINELOOKUPS << " linked " << seek_machine_name << " to " << found->getName() << "\n";
		}
		localised_names[seek_machine_name] = found;
	}
	return found;
}
std::ostream &operator<<(std::ostream &out, const MachineInstance &m) { return m.operator<<(out); }

/* Machine instances have different ways of reporting their 'current value', depending on the type of machine */

std::ostream &MachineValue::operator<<(std::ostream &out ) const {

	if (last_result != SymbolTable::Null) out << machine_instance->getName() << " (" << last_result.asString() <<")";
	else out << local_name << " (" <<  (( machine_instance) ? machine_instance->getName() : "NULL");
	out << ")";
	return out;
}

void MachineValue::flushCache() {
	machine_instance = 0;
	DynamicValue::flushCache();
}


class VariableValue : public DynamicValue {
	public:
		VariableValue(MachineInstance *mi): machine_instance(mi) { }
		virtual Value &operator()(MachineInstance *m) {
			if (machine_instance->getStateMachine()->token_id == ClockworkToken::VARIABLE
					|| machine_instance->getStateMachine()->token_id == ClockworkToken::CONSTANT) {
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
		MachineTimerValue(MachineInstance *mi): machine_instance(mi) { }
		Value &operator()(MachineInstance *m)  {
			assert(machine_instance);
			if (!machine_instance) { last_result = false; return last_result; }
			struct timeval now;
			gettimeofday(&now, NULL);
			long msecs = (long)get_diff_in_microsecs(&now, &machine_instance->start_time)/1000;
			last_result = msecs;
			//DBG_MSG << m->getName() << " update timer value " << last_result << "\n";
			return last_result;
		}
		Value &operator()()  {
			return operator()(machine_instance);
		}
		std::ostream &operator<<(std::ostream &out ) const {
			return out << machine_instance->getName() << ".TIMER (" << last_result <<")";
		}
		Value *getLastResult() { return &last_result; }
		DynamicValue *clone() const;
	protected:
		MachineInstance *machine_instance;
		friend class MachineInstance;
};

DynamicValue *MachineTimerValue::clone() const {
	MachineTimerValue *mtv = new MachineTimerValue(*this);
	return mtv;
}

MachineShadowInstance::MachineShadowInstance(InstanceType instance_type) : MachineInstance(instance_type) {
	shadow_machines.push_back(this);
}

MachineShadowInstance::MachineShadowInstance(CStringHolder name, const char * type, InstanceType instance_type)
	: MachineInstance(name, type, instance_type) {
	shadow_machines.push_back(this);
}

void MachineShadowInstance::idle() {
	return;
}

Action::Status MachineShadowInstance::setState(const State &new_state, bool resume) {
	NB_MSG << _name << " (shadow) setState("<<current_state<< "->" << new_state << ")\n";
	return MachineInstance::setState(new_state, resume);
}

Action::Status MachineShadowInstance::setState(const char *new_state, bool resume) {
	NB_MSG << _name << " (shadow) setState(" << new_state << ")\n";
	return MachineInstance::setState(new_state, resume);
}


MachineShadowInstance::~MachineShadowInstance() {
	return;
}


class CounterRateFilterSettings {
	public:
		long position;
		long velocity;
		bool property_changed;
		// analogue filter fields
		uint32_t noise_tolerance; // filter out changes with +/- this range
		int32_t last_sent; // this is the value to send unless the read value moves away from the mean
        int32_t last_pos;

		uint64_t start_t;
		uint64_t update_t;
        uint64_t last_update_t;
        SampleBuffer readings;
		MachineInstance *counter_machine;
		Value *noise_tolerance_val;
		long zero_count;
		CounterRateFilterSettings(unsigned int sz) : position(0), velocity(0), property_changed(false),
		noise_tolerance(20),last_sent(0), last_pos(0), start_t(0), update_t(0), last_update_t(0),
                readings(sz),counter_machine(0), noise_tolerance_val(0), zero_count(0) {
			struct timeval now;
			gettimeofday(&now, 0);
			update_t = start_t;
		}
};

CounterRateInstance::CounterRateInstance(InstanceType instance_type) :MachineInstance(instance_type) {
	settings = new CounterRateFilterSettings(32);
}
CounterRateInstance::CounterRateInstance(CStringHolder name, const char * type, InstanceType instance_type)
	: MachineInstance(name, type, instance_type) {
		settings = new CounterRateFilterSettings(32);
	}
CounterRateInstance::~CounterRateInstance() { delete settings; }

#if 0
bool CounterRateInstance::hasWork() { 
	struct timeval now;
	gettimeofday(&now, 0);
	return ( (uint64_t)now.tv_sec*1000000L + now.tv_usec >= (uint64_t) next_poll);
}
#endif

void CounterRateInstance::setValue(const std::string &property, Value new_value) {
	if (property == "VALUE") {
		if (new_value.kind == Value::t_symbol) {
			new_value = lookup(new_value.sValue.c_str());
		}
		long val;
		if (!new_value.asInteger(val)) val = 0;
		if (settings->property_changed) {
			settings->property_changed = false;
		}

		//reset once the buffers have been filled with zeros
		if (!val) ++settings->zero_count;
        else settings->zero_count = 0;
		if (settings->zero_count > settings->readings.length()) {
			settings->zero_count = 0;
			// reset buffers;
			//settings->start_t = settings->update_t;
			settings->readings.reset();
		}
        
        if ( (settings->update_t - settings->last_update_t)/1000 == 0 )
            return;
        settings->last_update_t = settings->update_t;
		uint64_t delta_t = settings->update_t - settings->start_t;
        settings->readings.append( val, delta_t);

		int32_t mean = (settings->readings.average(settings->readings.length()) + 0.5f);
		if ( abs(mean - settings->last_sent) > settings->noise_tolerance ) {
			settings->last_sent = mean;
		}
		settings->position = settings->last_sent;
		settings->velocity = filter(settings->position);

		MachineInstance::setValue(property, settings->velocity);
		MachineInstance::setValue("position", settings->position);
		MachineInstance *pos = lookup(parameters[0]);
		if (pos) pos->setValue("VALUE", settings->position);
	}
	else
		MachineInstance::setValue(property, new_value);
}

long CounterRateInstance::filter(long val) {
	if (settings->readings.length() < 4) return 0;
	//float speed = settings->positions.difference(settings->positions.length()-1, 0) / settings->times.difference(settings->times.length()-1,0) * 250000;
	//float speed = settings->positions.slopeFromLeastSquaresFit(settings->times) * 1000000;
    float speed = settings->readings.rate();
	return speed;
}

void CounterRateInstance::idle() {
	if (!io_interface) {
		struct timeval now;
		gettimeofday(&now, 0);
		uint64_t now_t = now.tv_sec * 1000000 + now.tv_usec;
		if (settings->update_t + 2000 < now_t) {
			long new_val = (long)((float)settings->position + settings->velocity * 2 / 1000.0f); //* (now_t-update_t) / 1000000.0f );
			setValue("VALUE", new_val);
		}
	}
}

void RateEstimatorInstance::setNeedsCheck() {
	//std::cout << _name << "::setNeedsCheck(), enabled: " << is_enabled 
	//	<< " has state machine? " << ( (state_machine) ? "yes" : "no") << "\n";
	MachineInstance::setNeedsCheck();
	busy_machines.insert(this);
}

void RateEstimatorInstance::idle() {
	MachineInstance *pos_m = lookup(parameters[0]);
	if (!pos_m) return;
	if (!settings) {
		if (settings) delete settings;
		if (pos_m->io_interface)
			settings = new CounterRateFilterSettings(8);
		else
			settings = new CounterRateFilterSettings(8);
	}
	if (is_enabled) { // && hasWork()) {
		double delta = 0;
		uint64_t curr_t = (pos_m->io_interface) ? pos_m->io_interface->read_time : process_time;

        delta = (double)(curr_t - settings->update_t) / 1000.0;
		if (!delta)  {
			return;
		}
        MachineInstance::idle();

		Value & pos_v = pos_m->getValue("VALUE");
		assert(pos_v.kind == Value::t_integer);
		assert(pos_v != SymbolTable::Null);
		long pos = pos_v.iValue;
		
/*
	  	std::cout << _name 
			<< " last_pos: " << settings->last_pos << " pos: " << pos_v.iValue
			<< " pos: " << pos_v.iValue
			<< " read time: " << curr_t
			<< " upd: " << settings->update_t
			<< " delta: " << delta
			<< " vel: " << ( (delta) ? (float)(pos - last_pos) * 1000.0/ delta : 0)<< "\n";
*/
		if (pos_m && pos_m->getValue("VALUE").asInteger(pos))
			setValue("VALUE", pos);
		if (pos != settings->last_pos || settings->velocity) {
			Trigger *trigger = new Trigger("RateEstimatorTimer");
			Scheduler::instance()->add(
				new ScheduledItem(10000, new FireTriggerAction(this, trigger)));
			trigger->release();
		}
		settings->last_pos = pos;
	}
}


RateEstimatorInstance::RateEstimatorInstance(InstanceType instance_type) :MachineInstance(instance_type) {
	settings = 0;
	if (!idle_time) idle_time = 50000;
}
RateEstimatorInstance::RateEstimatorInstance(CStringHolder name, const char * type, InstanceType instance_type)
	: MachineInstance(name, type, instance_type) {
		settings = 0;

		if (!idle_time) idle_time = 50000;
	}
RateEstimatorInstance::~RateEstimatorInstance() { delete settings; }

void RateEstimatorInstance::setValue(const std::string &property, Value new_value) {
	if (property == "VALUE") {
		if (new_value.kind == Value::t_symbol) {
			new_value = lookup(new_value.sValue.c_str());
		}
		MachineInstance *pos = lookup(parameters[0]);
		if (pos && pos->io_interface)
			settings->update_t = pos->io_interface->read_time;
		else {
			settings->update_t = process_time;
		}
		long val;
		if (!new_value.asInteger(val)) val = 0;
		if (settings->property_changed) {
			settings->property_changed = false;
		}

        // use current time - start time as t0 to avoid floating point resolution issues
		if (settings->start_t == 0) settings->start_t = settings->update_t;
		uint64_t delta_t = settings->update_t - settings->start_t;
		settings->position = (int32_t)val;
		settings->readings.append(settings->position, delta_t);
        settings->velocity = settings->readings.rate() * 1000000;
        //std::cout << _name << " adding reading: " << (int32_t)val <<" " << delta_t << " "
        //<<  settings->velocity << " "<< settings->readings.front << " " << settings->readings.back <<"\n";
		//settings->velocity = (int32_t)filter((int32_t)settings->position);

        //reset once the buffers have been filled with zeros
        if (!settings->velocity) ++settings->zero_count;
        else settings->zero_count = 0;
        if (settings->zero_count > settings->readings.BUFSIZE) {
            // reset buffers;
            //settings->start_t = settings->update_t;
            settings->readings.reset();
        }

		MachineInstance::setValue(property, settings->velocity);
		MachineInstance::setValue("position", settings->position);
	}
	else
		MachineInstance::setValue(property, new_value);
}

long RateEstimatorInstance::filter(long val) {
	if (settings->readings.length() < 2) return 0;
    float speed = settings->readings.rate() * 1000000;
	return speed;
}

MachineInstance::MachineInstance(InstanceType instance_type)
	: Receiver(""), 
	_type("Undefined"), 
	io_interface(0),
	mq_interface(0),
	owner(0),
	needs_check(0),
	uses_timer(false),
	my_instance_type(instance_type),
	state_change(0), 
	state_machine(0),
	current_state("undefined"),
	is_enabled(false),
	state_timer(0),
	locked(0),
	modbus_exported(none),
	saved_state("undefined"),
	current_state_val("undefined"),
	is_active(false),
	current_value_holder(0),
	last_state_evaluation_time(0),
	stable_states_stats("StableState processing"),
	message_handling_stats("Message handling"),
	data(0),
	idle_time(0),
	next_poll(0),
	is_traceable(false),
	published(0),
	cache(0),
	action_errors(0)
{
	if (!shared) shared = new SharedCache;
	cache = new Cache;
	state_timer.setDynamicValue(new MachineTimerValue(this));
	if (_type != "LIST" && _type != "REFERENCE")
		current_value_holder.setDynamicValue(new MachineValue(this, _name));
	if (instance_type == MACHINE_INSTANCE) {
		all_machines.push_back(this);
		Dispatcher::instance()->addReceiver(this);
		gettimeofday(&start_time, 0);
	}
}

MachineInstance::MachineInstance(CStringHolder name, const char * type, InstanceType instance_type)
	: Receiver(name), 
	_type(type), 
	io_interface(0), 
	mq_interface(0),
	owner(0),
	needs_check(0),
	uses_timer(false),
	my_instance_type(instance_type),
	state_change(0), 
	state_machine(0), 
	current_state("undefined"),
	is_enabled(false),
	state_timer(0),
	locked(0),
	modbus_exported(none),
	saved_state("undefined"),
	current_state_val("undefined"),
	is_active(false),
	current_value_holder(0),
	last_state_evaluation_time(0),
	stable_states_stats("StableState processing"),
	message_handling_stats("Message handling"),
	data(0),
	idle_time(0),
	is_traceable(false),
	published(0),
	cache(0),
	action_errors(0)
{
	if (!shared) shared = new SharedCache;
	cache = new Cache;
	state_timer.setDynamicValue(new MachineTimerValue(this));
	if (_type != "LIST" && _type != "REFERENCE")
		current_value_holder.setDynamicValue(new MachineValue(this, _name));
	if (instance_type == MACHINE_INSTANCE) {
		all_machines.push_back(this);
		Dispatcher::instance()->addReceiver(this);
		gettimeofday(&start_time, 0);
	}
}

MachineInstance::~MachineInstance() {
	all_machines.remove(this);
	automatic_machines.remove(this);
	active_machines.remove(this);
	Dispatcher::instance()->removeReceiver(this);
}

void simple_deltat(std::ostream &out, uint64_t dt) {
	if (dt > 60000000) out << std::setprecision(3) << (float)dt/60000000.0f << "m";
	else if (dt > 1000000) out << std::setprecision(3) << (float)dt/1000000.0f << "s";
	else if (dt>1000) out << std::setprecision(3) << (float)dt/1000.0f << "ms";
	else out << dt << "us";
}

void MachineInstance::describe(std::ostream &out) {
	out << "---------------\n" << _name << ": " << current_state.getName() << " "
		<< (enabled() ? "" : " DISABLED") <<  "\n"
		<< "  Class: " << _type << " instantiated at: " << definition_file
		<< " line:" << definition_line << "\n";
	if (locked) {
		out << "Locked by: " << locked->getName() << "\n";
	}
	if (parameters.size()) {
		for (unsigned int i=0; i<parameters.size(); i++) {
			Value p_i = parameters[i].val;
			if (p_i.kind == Value::t_symbol) {
				out << "  parameter " << (i+1) << " " << p_i.sValue << " (" << parameters[i].real_name 
					<< "), state: "
					<< (parameters[i].machine ? parameters[i].machine->getCurrent().getName(): "")
					<< (parameters[i].machine && !parameters[i].machine->enabled() ? "DISABLED" : "")
					<<  "\n";
			}
			else {
				out << "  parameter " << (i+1) << " " << p_i << " (" << parameters[i].real_name << ")\n";
			}
		}
		out << "\n";
	}
	if (io_interface) out << " io: " << *io_interface << "\n";
	if (locals.size()) {
		out << "Locals:\n";
		for (unsigned int i = 0; i<locals.size(); ++i) {
			if (locals[i].machine)
				out << "  " <<locals[i].val << " (" << locals[i].machine->getName()  << "):   " << locals[i].machine->getCurrent().getName() << "\n";
			else
				out << locals[i].val <<"  Missing machine\n";
		}
	}
	if (published) { out << "  published (" << published << ")\n"; }
	if (listens.size()) {
		out << "Listening to: \n";
		std::set<Transmitter *>::iterator iter = listens.begin();
		while (iter != listens.end()) {
			Transmitter *t = *iter++;
			MachineInstance *machine = dynamic_cast<MachineInstance*>(t);
			if (machine) {
				out << "  " << machine->getName() << "[" << machine->getId() << "]" << ":   " << (machine->enabled() ? machine->getCurrent().getName() : "DISABLED");
				if (machine->owner) out << " owner: " << (machine->owner ? machine->owner->getName() : "null");
				out << "\n";
			}
			else
				if (t) out << "  " << t->getName() << "\n";

		}
		out << "\n";
	}
	if (depends.size()) {
		out << "Dependant machines: \n";
		std::set<MachineInstance *>::iterator iter = depends.begin();
		while (iter != depends.end()) {
			MachineInstance *machine = *iter++;
			if (machine) {
				out << "  " << machine->getName() << "[" << machine->getId() << "]" << ":   " << (machine->enabled() ? machine->getCurrent().getName() : "DISABLED");
				if (machine->owner) out << " owner: " << (machine->owner ? machine->owner->getName() : "null");
				out << "\n";
			}
			else
				out << " NULL\n";

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
		std::map<std::string, MachineCommandTemplate*>::iterator cmd_iter = state_machine->commands.begin();
		const char *delim = "";
		while (cmd_iter != state_machine->commands.end()) {
			const std::pair<std::string, MachineCommandTemplate*> &curr = *cmd_iter++;
			out << delim << curr.first;
			delim = ", ";
		}
		out << "\n";
	}

	if (!mail_queue.empty()) {
		out << "queued messages: " << mail_queue.size() << "\n";
		std::list<Package*>::iterator evt_iter = pending_events.begin();
		while (evt_iter != pending_events.end()) {
			Package *pkg = *evt_iter++;
			out << *pkg << "\n";
		}
	}

	if (action_errors) out << "action errors seen: " << action_errors << "\n";
	if (!active_actions.empty()) {
		out << "active actions: (" << active_actions.size() << ")\n";
		displayActive(out);
		out << "\n";
	}
	if (properties.size()) {
		out << "properties:\n  ";
		out << properties << "\n\n";
	}
	if (locked) {
		out << "locked: " << locked->getName() << "\n";
	}
	if (uses_timer)
		out << "Uses timer in stable state evaluation\n";
	const Value *current_timer_val = getTimerVal();
	out << "Timer: " << *current_timer_val << "\n";
	if (stable_states.size() || state_machine->token_id == ClockworkToken::LIST) {
		struct timeval now;
		gettimeofday(&now, 0);
		long now_t = now.tv_sec * 1000000 + now.tv_usec;
		uint64_t delta = now_t - last_state_evaluation_time;
		out << "Last stable state evaluation (";
		simple_deltat(out, delta);
		out << "):\n";
		for (unsigned int i=0; i<stable_states.size(); ++i) {
			out << "  " << stable_states[i].state_name << ": " << stable_states[i].condition.last_evaluation << "\n";
			if (stable_states[i].condition.last_result == true) {
				if (stable_states[i].subcondition_handlers && !stable_states[i].subcondition_handlers->empty()) {
					std::list<ConditionHandler>::iterator iter = stable_states[i].subcondition_handlers->begin();
					while (iter != stable_states[i].subcondition_handlers->end()) {
						ConditionHandler &ch = *iter++;
						out << "      " << ch.command_name <<" ";
						if (ch.command_name != "FLAG" && ch.trigger) out << (ch.trigger->fired() ? "fired" : "not fired");
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
			if (s.condition.predicate) s.condition.predicate->flushCache();
		}
		setNeedsCheck();
	}
}

void MachineInstance::stopListening(MachineInstance *m) {
	listens.erase(m);
	for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
		StableState &s = stable_states[ss_idx];
		if (s.condition.predicate) s.condition.predicate->flushCache();
	}
	setNeedsCheck();
}

bool checkDepend(std::set<Transmitter*>&checked, Transmitter *seek, Transmitter *test) {
	if (seek == test) return true;
	MachineInstance *mi = dynamic_cast<MachineInstance*>(test);
	if (mi)	{ 
		BOOST_FOREACH(Transmitter *machine, mi->listens) {
			if (!checked.count(machine)) {
				checked.insert(machine);
				if (checkDepend(checked, seek, machine)) return true;
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
		std::set<MachineInstance*>::iterator found = depends.find(m);
		depends.erase(found);
		DBG_M_DEPENDANCIES << _name << " removed dependant machine " << m->getName() << "\n";
	}
}

bool MachineInstance::dependsOn(Transmitter *m) {
	std::set<Transmitter*>checked;
	return checkDepend(checked, m, this);
}


bool MachineInstance::needsCheck() {
	return needs_check != 0;
}

void MachineInstance::resetNeedsCheck() {
	needs_check = 0;
}

void MachineInstance::idle() {
	if (error_state) {
		return;
	}
	if (!is_enabled) {
		return;
	}
	if (!is_active) {
		if (!cache->reported_error) {
			char buf[100];
			snprintf(buf, 100, "Error: %s::idle() called but this machine is not active", _name.c_str());
			MessageLog::instance()->add(buf);
			DBG_MSG << buf << "\n";
			cache->reported_error = true;
		}
		return;
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
			stop(curr); // avoid double queueing
			Action::Status res = (*curr)();
			if (res == Action::Failed) {
				std::stringstream ss; ss << _name << ": Action " << *curr << " failed: " << curr->error() << "\n";
				MessageLog::instance()->add(ss.str().c_str());
				NB_MSG << ss.str() << "\n";
			}
			else if (res != Action::Complete) {
				DBG_M_ACTIONS << "Action " << *curr << " is not complete, waiting...\n";
				curr->release();
				return;
			}
		}
		else if (!curr->complete())  {
			DBG_M_ACTIONS << "Action " << *curr << " is not still not complete\n";
			curr->release();
			return;
		}
		Action *last = curr;
		if (last->getStatus() == Action::Failed) {
			NB_MSG << "Action " << (*curr) << " failed\n";
			++action_errors;
		}
		curr = executingCommand();
		if (curr && curr == last) {
			DBG_M_ACTIONS << "Action " << *curr << " failed to remove itself when complete. doing so manually\n";
			stop(curr);
			curr->release();
			curr = executingCommand();
			assert(curr != last);
		}
		else {
			last->release();
		}
	}
	// TBD this could be an infinite loop if there is a way for handle() to cause a push
	while (!mail_queue.empty()){
		if (!mail_queue.empty()) {
			boost::mutex::scoped_lock(q_mutex);
			DBG_M_MESSAGING << _name << " has " <<  mail_queue.size() << " messages waiting\n";
			Package p = mail_queue.front();
			DBG_M_MESSAGING << _name << " found package " << p << "\n";
			mail_queue.pop_front();
			handle(p.message, p.transmitter, p.needs_receipt);
		}
	}
	if (mail_queue.empty() && active_actions.empty()) has_work = false;
	return;
}

uint64_t nowMicrosecs() {
	struct timeval now;
	gettimeofday(&now, 0);
	return (uint64_t) now.tv_sec*1000000 + (uint64_t)now.tv_usec;
}

uint64_t nowMicrosecs(const struct timeval &now) {
    return (uint64_t) now.tv_sec*1000000 + (uint64_t)now.tv_usec;
}

int64_t get_diff_in_microsecs(const struct timeval *now, const struct timeval *then) {
//   uint64_t t = (now->tv_sec - then->tv_sec);
//   t = t * 1000000 + (now->tv_usec - then->tv_usec);
//	return t;
	uint64_t now_t = now->tv_sec * 1000000L + now->tv_usec;
	uint64_t then_t = then->tv_sec * 1000000L + then->tv_usec;
	int64_t t = now_t - then_t;
	return t;
}

int64_t get_diff_in_microsecs(uint64_t now_t, const struct timeval *then) {
	uint64_t then_t = then->tv_sec * 1000000L + then->tv_usec;
	int64_t t = now_t - then_t;
	return t;
}

int64_t get_diff_in_microsecs(const struct timeval *now, uint64_t then_t) {
	uint64_t now_t = now->tv_sec * 1000000L + now->tv_usec;
	int64_t t = now_t - then_t;
	return t;
}

// Machine idle processing
/*
   Each machine contains a timer that indicates how long the machine has been
   in a particular state. This polling loop first ensures that the timer value
   is updated for all machines and then calles idle() on each machine.
 */

const Value *MachineInstance::getTimerVal() {
	MachineTimerValue *mtv = dynamic_cast<MachineTimerValue*>(state_timer.dynamicValue());
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

#if 1
bool MachineInstance::processAll(uint32_t max_time, PollType which) {

	uint64_t start_processing = nowMicrosecs();
	process_time = start_processing;

	std::list<Package*>::iterator evt_iter = pending_events.begin();
	while (evt_iter != pending_events.end() ) {
		Package *pkg = *evt_iter;
		evt_iter = pending_events.erase(evt_iter);
		MachineInstance *mi = dynamic_cast<MachineInstance*>(pkg->receiver);
		if (mi) mi->execute(*(pkg->message), pkg->transmitter);
		delete pkg;
		++total_process_calls;
		if (mi) mi->setNeedsCheck();
	}

	process_time = nowMicrosecs();

	std::set<MachineInstance*>::iterator busy_it = busy_machines.begin();
	while (busy_it != busy_machines.end() ) {
		MachineInstance *mi = *busy_it;
		mi->idle();
		if (mi->state_machine && mi->state_machine->plugin) mi->state_machine->plugin->poll_actions(mi);
		if ((mi->state_machine && mi->state_machine->plugin) || !mi->executingCommand()) {
			busy_it = busy_machines.erase(busy_it);
			if (mi->is_active) pending_state_change.insert(mi);
		}
		else busy_it++;
	}

#if 0
  uint64_t now = nowMicrosecs();
	total_processing_time += now - start_processing;
	if (now - start_processing < max_time) ++loop_count; // completed a pass through all machines
	
	if (!shared->system) shared->system = MachineInstance::find("SYSTEM");
	MachineInstance *system = shared->system;
	system->setValue("PENDING_STATE_CHANGE", pending_state_change.size());
	if (loop_count % 100 == 1) {
		system->setValue("AVG_PROCESS_CALLS_PER_KCYCLE", total_process_calls*1000 / loop_count);
		system->setValue("AVG_MACHINES_WITH_WORK_PER_KCYCLE", total_machines_with_work*1000 / loop_count);
		system->setValue("AVG_WORK_PER_KCYCLE", total_idle_calls*1000 / loop_count);
		system->setValue("CYCLES", loop_count);
		system->setValue("AVG_PLUGIN_WORK_PER_KCYCLE", total_plugins_with_work * 1000 / loop_count);
		system->setValue("SHORTCUTS_TAKEN_PER_KCYCLE", shortcuts * 1000 / loop_count);
		system->setValue("AVG_PROCESSING_TIME_PER_KCYCLE", (long)(total_processing_time * 1000 / loop_count));
		system->setValue("AVG_ABORTS_PER_KCYCLE", total_aborts * 1000/ loop_count);
	}
#endif
	return true;
}
#else
bool MachineInstance::processAll(uint32_t max_time, PollType which) {
	std::list<Package*>::iterator evt_iter = pending_events.begin();
	while (evt_iter != pending_events.end()) {
		Package *pkg = *evt_iter;
		evt_iter = pending_events.erase(evt_iter);
		MachineInstance *mi = dynamic_cast<MachineInstance*>(pkg->receiver);
		if (mi) mi->execute(*(pkg->message), pkg->transmitter);
		delete pkg;
	}

	static int point_token = ClockworkToken::POINT;
	static std::list<MachineInstance *>::iterator iter = active_machines.begin();
	bool builtins = false;
	if (iter == active_machines.begin()) {
		struct timeval now;
		gettimeofday(&now, NULL);
		saved_loop_count = loop_count;
		process_time = now.tv_sec * 1000000 + now.tv_usec;
		if (which == BUILTINS)
			builtins = true;
		if (last_num_machines_with_work == 0 && num_machines_with_work == 0) {
			shortcuts++;
			return true;
		}
		else DBG_AUTOSTATES << num_machines_with_work << " machines with work at MachineInstance::processAll\n";
		last_num_machines_with_work = num_machines_with_work;
		num_machines_with_work = 0; // counts how many machines have work remaining at the end of this process
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	uint64_t start_processing = now.tv_sec * 1000000 + now.tv_usec;
	const int block_size = 20;
	int count = block_size;
	while (iter != active_machines.end()) {
		/* To compute a counterrate, the device needs a continual suppliy of input readings. 
		   The process of polling the hardware will normally do this but when running a simulation
		   we need some help. The following hack provides the solution until a proper method can be developed.
		 */
		MachineInstance *m = *iter++;
		bool point = m->state_machine->token_id == point_token;
		if ( (builtins && point) || (!builtins && (!point || m->mq_interface) && m->enabled() ) ) {
			if (m->hasWork()) { /*std::cout << m->_name << " has work\n";*/ ++total_machines_with_work; }
			if (m->hasWork() || !m->active_actions.empty() ) {
				DBG_ACTIONS << m->getName() << " working in state: " << m->current_state.getName() << "\n";
				m->idle();
				++total_idle_calls;
			}
			if (m->state_machine && m->state_machine->plugin && m->state_machine->plugin->poll_actions) {
				m->state_machine->plugin->poll_actions(m);
				++total_plugins_with_work;
				//TBD                ++num_machines_with_work;
			}
			if (m->hasWork() || !m->active_actions.empty()) {
				DBG_ACTIONS << m->getName() << " has work\n";
				++num_machines_with_work;
				DBG_ACTIONS << m->_name << " ADDED to machines with work " << num_machines_with_work << "\n";
			}
		}
		count--;
		if (count<=0) {
			struct timeval now;
			gettimeofday(&now, NULL);
			uint64_t now_t = now.tv_sec * 1000000 + now.tv_usec;
			if (now_t - start_processing > max_time) {
				total_processing_time += now_t - start_processing;
				total_aborts++;
				return false; // ran out of time to finish
			}
			count = block_size;
		}
	}
	if (num_machines_with_work == 0) {
		DBG_ACTIONS << " no more actions outstanding\n";
	}
	else {
		DBG_ACTIONS << " more actions to run\n";
	}
	gettimeofday(&now, NULL);
	uint64_t now_t = now.tv_sec * 1000000 + now.tv_usec;
	total_processing_time += now_t - start_processing;
	++loop_count; // completed a pass through all machines
	total_process_calls += loop_count - saved_loop_count;
#if 0
	if (!shared->system) shared->system = MachineInstance::find("SYSTEM");
	MachineInstance *system = shared->system;
	assert(system);
	if (loop_count % 100 == 1) {
		system->setValue("AVG_PROCESS_CALLS_PER_KCYCLE", total_process_calls*1000 / loop_count);
		system->setValue("AVG_MACHINES_WITH_WORK_PER_KCYCLE", total_machines_with_work*1000 / loop_count);
		system->setValue("AVG_WORK_PER_KCYCLE", total_idle_calls*1000 / loop_count);
		system->setValue("CYCLES", loop_count);
		system->setValue("AVG_PLUGIN_WORK_PER_KCYCLE", total_plugins_with_work * 1000 / loop_count);
		system->setValue("SHORTCUTS_TAKEN_PER_KCYCLE", shortcuts * 1000 / loop_count);
		system->setValue("AVG_PROCESSING_TIME_PER_KCYCLE", (long)(total_processing_time * 1000 / loop_count));
		system->setValue("AVG_ABORTS_PER_KCYCLE", total_aborts * 1000/ loop_count);
	}
#endif
	iter = active_machines.begin(); // prepare for the next cycle
	return true; // complete the cycle
}
#endif

#if 1

void MachineInstance::checkPluginStates() {
	//std::list<uint64_t> stats;
	//uint64_t start_processing = nowMicrosecs();
	std::set<MachineInstance *>::iterator pl_iter = plugin_machines.begin();
	while (pl_iter != plugin_machines.end())  {
		MachineInstance *m = *pl_iter++;
		if (!m->is_enabled) continue;
		//if (m->next_poll > start_processing) { continue; }
		//uint64_t start = nowMicrosecs();
		if (m->state_machine && m->state_machine->plugin)  {
			if ( m->state_machine->plugin->state_check) {
				m->state_machine->plugin->state_check(m);
			}
			if ( m->state_machine->plugin->poll_actions) {
				m->state_machine->plugin->poll_actions(m);
			}
		}
		//uint64_t delta = nowMicrosecs() - start;
		//stats.push_back(delta);
	}
#if 0
  std::list<uint64_t>::iterator iter = stats.begin();
  while (iter != stats.end()) {
		uint64_t x = *iter++;
		std::cout << x << " ";
	}
	if (!stats.empty()) std::cout << "\n";
	DBG_MSG << "-- " << (nowMicrosecs() - start_processing) << "\n";
#endif
}

bool MachineInstance::checkStableStates(uint32_t max_time) {
	total_machines_needing_check = 0;
	std::set<MachineInstance *>::iterator iter = MachineInstance::pending_state_change.begin();
	while (iter != MachineInstance::pending_state_change.end() ) {
		MachineInstance *mi = *iter;
		if (mi->executingCommand() == 0) {
			// leave the state check on the queue until it is stable
			if (!mi->setStableState()) iter = pending_state_change.erase(iter); else iter++;
		}
		else {
			busy_machines.insert(mi);
			iter++;
		}
	}
	return true;
}
#else

bool MachineInstance::checkStableStates(uint32_t max_time) {
#ifdef USE_EXPERIMENTAL_IDLE_LOOP
	if (last_machines_needing_check == 0 && total_machines_needing_check == 0) return true;
#endif
	last_machines_needing_check = total_machines_needing_check;
	DBG_AUTOSTATES << total_machines_needing_check << " machines need state check in checkStableStates\n";
	total_machines_needing_check = 0;
	static std::list<MachineInstance *>::iterator iter = MachineInstance::automatic_machines.begin();

	struct timeval now;
	gettimeofday(&now, NULL);
	uint64_t start_processing = now.tv_sec * 1000000 + now.tv_usec;
	const int block_size = 5;
	int count = block_size;
	while (iter != MachineInstance::automatic_machines.end()) {
		MachineInstance *m = *iter++;
		DBG_AUTOSTATES  << " check stable states: " << m->getName() << "\n";
		if ( m->enabled() && m->executingCommand() == NULL
				&& (m->needsCheck() || m->state_machine->token_id == ClockworkToken::tokCONDITION ) && m->next_poll <= start_processing )
		{
			DBG_AUTOSTATES  << "calling " << m->getName() << "::setStableState()\n";
			m->setStableState();
			if (m->state_machine && m->state_machine->plugin && m->state_machine->plugin->state_check) {
				m->state_machine->plugin->state_check(m);
				DBG_AUTOSTATES  << "flagging state checks for " << m->getName() << " because it has a plugin\n";
				++total_machines_needing_check;
			}
			else if (m->enabled() && m->executingCommand() == NULL
					&& (m->needsCheck() || m->state_machine->token_id == ClockworkToken::tokCONDITION ))
			{
				DBG_AUTOSTATES  << "flagging state checks for " << m->getName() << " because it is a condition or needs a check\n";
				++total_machines_needing_check;
			}
		}
		else {
			if (m->needsCheck()) {
				DBG_AUTOSTATES  << m->getName() << " not checked\n";
			}
		}
		// else if ( m->enabled() && m->executingCommand() == NULL
		//     && (m->needsCheck() || m->state_machine->token_id == ClockworkToken::tokCONDITION ) 
		//	&& m->next_poll > start_processing ) {
		//std::cout << m->getName() << " skipping stable state check due to throttling\n";
		//}
		//   else if ( m->enabled() && m->executingCommand() != NULL
		//       && (m->needsCheck() || m->state_machine->token_id == ClockworkToken::tokCONDITION ) ) {
		//	std::cout << m->getName() << " skipped stable state tests due to active actions\n";
		//	m->displayActive(std::cout);
		//}
		count--;
		if (count<=0) {
			struct timeval now;
			gettimeofday(&now, NULL);
			uint64_t now_t = now.tv_sec * 1000000 + now.tv_usec;
			if (now_t - start_processing > max_time) {
				DBG_AUTOSTATES << " aborting stable state checks\n";
				return false; // ran out of time to finish
			}
			count = block_size;
		}
	}
	iter = MachineInstance::automatic_machines.begin();
	if (total_machines_needing_check == 0) {
		DBG_AUTOSTATES << " all states are stable (" << automatic_machines.size() << " machines)\n";
	}
	return true;
}
#endif

void MachineInstance::displayAutomaticMachines() {
	BOOST_FOREACH(MachineInstance *m, MachineInstance::automatic_machines) {
		std::cout << *m << "\n";
	}
}

void MachineInstance::displayAll() {
	BOOST_FOREACH(MachineInstance *m, all_machines) {
		std::cout << "-------" << m->getName() << "\n";
		std::cout << *m << "\n";
	}
}


// linear search through the machine list, tbd performance..
MachineInstance *MachineInstance::find(const char *name) {
	std::string machine(name);
	if (machine.find('.') != std::string::npos) {
		machine.erase(machine.find('.'));
		MachineInstance *mi = find(machine.c_str());
		if (mi) {
			std::string shortname(name);
			shortname = shortname.substr(shortname.find('.')+1);
			return mi->lookup(shortname);
		}
	}
	else {
		std::list<MachineInstance*>::iterator iter = all_machines.begin();
		while (iter != all_machines.end()) {
			MachineInstance *m = *iter++;
			if (m->_name == name) return m;
		}
	}
	return 0;
}

void MachineInstance::addParameter(Value param, MachineInstance *mi) {
	parameters.push_back(param);
	if (!mi && param.kind == Value::t_symbol) {
		mi = lookup(param);
		if (mi) {
			if (!param.cached_machine) param.cached_machine = mi;
			parameters[parameters.size()-1].real_name = mi->fullName();
		}
	}
	else if (mi) {
		parameters[parameters.size()-1].real_name = mi->fullName();
	}
	parameters[parameters.size()-1].machine = mi;
	if (mi) {
		addDependancy(mi);
		listenTo(mi);
		mi->addDependancy(this);
		mi->listenTo(this);
	}
	//if (_type == "LIST" || _type == "REFERENCE")
	setNeedsCheck();
	notifyDependents();
	//}
}

void MachineInstance::removeParameter(int which) {
	if (which <0 || which >= (int)parameters.size()) return;
	MachineInstance *m = parameters[which].machine;
	if (m) {
		m->removeDependancy(this);
		m->stopListening(this);
		removeDependancy(m);
		stopListening(m);
	}
	parameters.erase(parameters.begin()+which);
	setNeedsCheck();
	notifyDependents();
}

void MachineInstance::addLocal(Value param, MachineInstance *mi) {
	locals.push_back(param);
	locals[locals.size()-1].machine = mi;
	if (!mi && param.kind == Value::t_symbol) mi = lookup(param.asString().c_str());
	std::set<MachineInstance*>::iterator dep_iter = depends.begin();
	while (dep_iter != depends.end()) {
		MachineInstance *dep = *dep_iter++;
		if (mi) {
			mi->addDependancy(dep);
			dep->listenTo(mi);
		}
		dep->setNeedsCheck();
	}

	if (mi) {
		mi->addDependancy(this); listenTo(mi);
		for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
			StableState &s = stable_states[ss_idx];
			if (s.condition.predicate) s.condition.predicate->flushCache();
		}
	}
	setNeedsCheck();
	if (_type == "LIST" || _type == "REFERENCE") {
		notifyDependents();
		/*if (owner) {
		  std::set<MachineInstance*>::iterator dep_iter = owner->depends.begin();
		  while (dep_iter != owner->depends.end()) {
		  MachineInstance *dep = *dep_iter++;
		  if (mi){
		  mi->addDependancy(dep);
		  dep->listenTo(mi);
		  }
		  dep->setNeedsCheck();
		  }
		  }*/
	}
}

void MachineInstance::removeLocal(int index) {
	if ((int)locals.size() <= index) return;
	Parameter &p = locals[index];
	if (p.machine) {
		stopListening(p.machine);
		p.machine->removeDependancy(this);
	}
	// each dependency of the reference should no longer be dependant on changees to the entry
	std::set<MachineInstance*>::iterator dep_iter =depends.begin();
	if (p.machine)
		while (dep_iter != depends.end()) {
			MachineInstance *dep = *dep_iter++;
			p.machine->removeDependancy(dep);
			dep->stopListening(p.machine);
			dep->setNeedsCheck();
		}
	localised_names.erase(p.val.sValue);
	std::vector<Parameter>::iterator iter = locals.begin();
	while (index-- && iter != locals.end()) iter++;
	if (iter != locals.end()) locals.erase(iter);
	setNeedsCheck();
	if (_type == "LIST" || _type == "REFERENCE") {
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
			long pd;
			if (p.second.asInteger(pd)) idle_time = pd;
		}
		else if (p.first == "TRACEABLE") {
			if (p.second == "TRUE") is_traceable = true;
			if (p.second == "FALSE") is_traceable = false;
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

std::ostream &MachineInstance::operator<<(std::ostream &out)const  {
	out << _name << "(" << id <<")" << "<" << _type << ">\n";
	if (properties.begin() != properties.end()) {
		out << "  Properties: " << properties << "\n"; 
	}
	if (!parameters.empty()) {
		for (unsigned int i=0; i<parameters.size(); ++i) {
			out << "  Parameter " << i << ": " << parameters[i].val << " real name: " << parameters[i].real_name << " ";
			if (!parameters[i].properties.empty())
				out << "(" << parameters[i].properties << ")";
			MachineInstance *mi = parameters[i].machine;
			if (mi) out << "=>" << mi->getName()<< ":"<< mi->id<<" ";
			out << "\n";
		}
	}
	if (!locals.empty()) {
		for (unsigned int i=0; i<locals.size(); ++i) {
			out << "  Local " << i << " " << locals[i].val;
			if (!locals[i].properties.empty())
				out << "(" << locals[i].properties << ") ";
			MachineInstance *mi = locals[i].machine;
			if (mi) out << "=>" << (mi->getName())<< ":"<< mi->id<<" ";
		}
		out << "\n";
	}
	if (uses_timer) out << "Uses Timer\n";
	if (!depends.empty()) {
		out << "Dependant machines: ";
		BOOST_FOREACH(MachineInstance *dep, depends) out << dep->getName() << ",";
		out << "\n";
	}
	out << "  Defined at: " << definition_file << ":" << definition_line << "\n";
	return out;
}

bool MachineInstance::stateExists(State &seek) {
	//return (state_machine->state_names.count(seek.getName()) != 0);

	std::list<State>::const_iterator iter = state_machine->states.begin();
	while (iter != state_machine->states.end()) {
		if (*iter == seek) return true;
		iter++;
	}
	for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
		if (stable_states[ss_idx].state_name == seek.getName()) return true;
	}
	return false;
}

/*
   Machines hear messages only when registered to do so, the rules are:
 * all machines receive messages from themselves
 * machines receive messages from objects they are listening to
 * commands in the transition table are public and can be sent by anyone
 */
bool MachineInstance::receives(const Message&m, Transmitter *from) {
	if (!enabled()) return false;
	// passive machines do not receive messages
	//if (!is_active) return false;
	// all active machines receive messages from themselves
	if (from == this || (io_interface && from == io_interface) ) {
		DBG_M_MESSAGING << "Machine " << getName() << " receiving " << m << " from itself\n";
		return true; 
	}    
	// machines receive messages from objects they are listening to
	if (std::find(listens.begin(), listens.end(), from) != listens.end()) {
		DBG_M_MESSAGING << "Machine " << getName() << " receiving " << m << " from " << from->getName() << "\n";
		return true;
	}
	if (commands.count(m.getText()) != 0) {
		return true;    // items in the command table are public and can be sent by anyone
	}

	// commands in the transition table are public and can be sent by anyone
	std::list<Transition>::const_iterator iter = transitions.begin();
	while (iter != transitions.end()) {
		const Transition &t = *iter++;
		if (t.trigger == m) return true;
	}
	DBG_MESSAGING << "Machine " << getName() << " ignoring " << m << " from " << ((from) ? from->getName() : "NULL") << "\n";
	return false;
}

Action::Status MachineInstance::setState(const char *sn, bool resume) {
	char buf[150];
	if (!state_machine) {
		snprintf(buf, 150, "Error: setting state to %s on a machine (%s) with no statemachine", sn, _name.c_str());
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
	return setState(*s, resume);
}

void MachineInstance::forceStableStateCheck() { 
	DBG_AUTOSTATES << "stable state check forced\n";
	total_machines_needing_check++; 
}

Action::Status MachineInstance::setState(const State &new_state, bool resume) {

	if (!hasState(new_state)) {
		Value &s = getValue(new_state.getName().c_str());
		State propertyState(s.asString().c_str());
		if (!hasState(propertyState)) {
			char buf[150];
			snprintf(buf, 150, "Error: unknown state: '%s' on %s", new_state.getName().c_str(), _name.c_str());
			MessageLog::instance()->add(buf);
			DBG_MSG << buf << "\n";
			return Action::Failed;
		}
		return setState(propertyState);
	}
	Action::Status stat = Action::Complete;

#if 0
	// TBD Why do we send the modbus state change here. This should not be done
	// until later, if the state actually changes

	// update the Modbus interface for self 
	if (published && (modbus_exported == discrete || modbus_exported == coil) ) {
		if (!modbus_exports.count(_name)) {
			char buf[100];
			snprintf(buf, 100, "%s Internal Error: modbus export info not found", getName().c_str());
			MessageLog::instance()->add(buf);
			NB_MSG << buf << "\n";
		}
		else {
			ModbusAddress ma = modbus_exports[_name];
			if (ma.getSource() == ModbusAddress::machine && ( ma.getGroup() != ModbusAddress::none) ) {
				if (new_state.getName() == "on") { // just turned on
					DBG_MODBUS << _name << " machine came on; triggering a modbus message\n";
					ma.update(this, 1);
				}
				else if (current_state.getName() == "off") {
					DBG_MODBUS << _name << " machine went off; triggering a modbus message\n";
					ma.update(this, 0);
				}		
			}
			else {
				DBG_M_MSG << _name << ": exported: " << modbus_exported << " (" << ma << ") but address is of type 'none'\n";
			}
		}
	}
#endif

	if (resume || current_state != new_state) {
		std::string last = current_state.getName();

#ifndef DISABLE_LEAVE_FUNCTIONS
		/* notify everyone we are leaving a state */
		// TBD use notifyDependents()

		/* do not send a leave message for the state if the state is not changing (ie in resume) */
		if (enabled() && current_state != new_state) {
			std::string txt = _name + "." + current_state.getName() + "_leave";
			Message msg(txt.c_str());
			stat = execute(msg, this);
			if (stat == Action::Failed || (!isActive() && stat != Action::Complete) ) {
				char buf[200];
				snprintf(buf, 200, "%s %s leave action did not complete (%s)", _name.c_str(), txt.c_str(), actionStatusName(stat));
				MessageLog::instance()->add(buf);
				NB_MSG << buf << "\n";
			}

			std::set<MachineInstance*>::iterator dep_iter = depends.begin();
			while (dep_iter != depends.end()) {
				MachineInstance *dep = *dep_iter++;
				if (this == dep) continue;

				// note that the following test prevents us from resuming a
				// blocked action in dep and that may be bad but the test is
				// not used in the case of the ENTER handler, below and there
				// is no need to resume twice
				if (!dep->receives(msg, this)) continue;
				Action *act = dep->executingCommand();
				if (act) {
					if (act->getStatus() == Action::Suspended) act->resume();
					DBG_M_MESSAGING << dep->getName() << "[" << dep->getCurrent().getName() << "]" << " is executing " << *act << "\n";
				}

				// execute the message on the dependant machine or queue it for later processing
				if (!dep->isActive())
					dep->execute(msg, this);
				else {
					Package *p = new Package(this, dep, msg, false);
					Dispatcher::instance()->deliver(p);
				}
				//dep->setNeedsCheck();
			}
		}
#endif
		gettimeofday(&start_time,0);
		gettimeofday(&disabled_time,0);
		DBG_STATECHANGES << fullName() << " changing from " << current_state << " to " << new_state << "\n";
		current_state = new_state;
		current_state_val = new_state.getName();
		properties.add("STATE", current_state.getName().c_str(), SymbolTable::ST_REPLACE);
		// publish the state change if we are a publisher
		if (mq_interface) {
			if (_type == "POINT" && properties.lookup("type") == "Output") {
				mq_interface->publish(properties.lookup("topic").asString(), new_state.getName(), this);
			}
		}
		if (published && !modbus_exports.empty()) {
			// update the Modbus interface for the state
			if (modbus_exports.count(_name + "." + last)){
				ModbusAddress ma = modbus_exports[_name + "." + last];
				DBG_MODBUS << _name << " leaving state " << last << " triggering a modbus message " << ma << "\n";
				assert(ma.getSource() == ModbusAddress::state);
				ma.update(this, 0);
			}
			if (modbus_exports.count(_name + "." + current_state.getName())){
				ModbusAddress ma = modbus_exports[_name + "." + current_state.getName()];
				DBG_MODBUS << _name << " active state " << current_state.getName() << " triggering a modbus message " << ma << "\n";
				assert(ma.getSource() == ModbusAddress::state);
				ma.update(this, 1);
			}
		}
		/* TBD optimise the timer triggers to only schedule the earliest trigger
		   StableState *earliestTimerState = NULL;
		   unsigned long ealiestTimer = (unsigned long) -1L;
		 */

		for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
			StableState &s = stable_states[ss_idx];
			if (s.uses_timer) {
				long timer_val;
				// first disable any trigger that may still be enabled
				if ( s.trigger) {
					DBG_M_SCHEDULER << _name << " clearing trigger for state " << s.state_name << "\n";
					if (s.trigger->enabled() ) {
						DBG_M_SCHEDULER << _name << " disabling " << s.trigger->getName() << "\n";
						s.trigger->disable();
					}
					s.trigger->release();
					s.trigger = 0;
				}
				s.condition.predicate->clearTimerEvents(this);

				// BUG here. If the timer comparison is '>' (ie Timer should be > the given value
				//   we should trigger at v.iValue+1
				if (s.timer_val.kind == Value::t_symbol) {
					Value v = getValue(s.timer_val.sValue);
					if (v.kind != Value::t_integer) {
						NB_MSG << _name << " Error: timer value for state " << s.state_name << " is not numeric\n";
						NB_MSG << "timer_val: " << s.timer_val << " lookup value: " << v << " type: " << v.kind << "\n";
						continue;
					}
					else
						timer_val = v.iValue;
				}
				else if (s.timer_val.kind == Value::t_integer)
					timer_val = s.timer_val.iValue;
				else {
					DBG_M_SCHEDULER << _name << " Warning: timer value for state " << s.state_name << " is not numeric\n";
					NB_MSG << "timer_val: " << s.timer_val << " type: " << s.timer_val.kind << "\n";
					continue;
				}
				if (s.condition.predicate->op == opGT) timer_val++;
				else if (s.condition.predicate->op == opLT) --timer_val;
				DBG_M_SCHEDULER << _name << " Scheduling timer for " << timer_val << "ms\n";
				// prepare a new trigger. note: very short timers will still be scheduled
				// TBD move this outside of the loop and only apply it for the earliest timer
				s.trigger = new Trigger("SSTimer");
				Scheduler::instance()->add(new ScheduledItem(timer_val*1000, new FireTriggerAction(this, s.trigger)));
			}
			if (s.subcondition_handlers) 
			{
				std::list<ConditionHandler>::iterator iter = s.subcondition_handlers->begin();
				while (iter != s.subcondition_handlers->end()) {
					ConditionHandler &ch = *iter++;
					//setup triggers for subcondition handlers

					if (ch.uses_timer) {
						long timer_val;
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
								Value &tv = node->getTimerValue();
								/*MachineTimerValue *mtv = dynamic_cast<MachineTimerValue*>(&tv);
								if (!mtv)
									int x=1;
								mtv->operator()();
								*/if (tv == SymbolTable::Null) continue;
								if (tv.kind == Value::t_symbol || tv.kind == Value::t_string) {
									Value v = getValue(tv.sValue);
									if (v.kind != Value::t_integer) {
										DBG_MSG << _name << " Warning: timer value "<< v << " for state " 
											<< s.state_name << " subcondition is not numeric\n";
										continue;
									}

									if (v.iValue>=0 && v.iValue < timer_val) {
										timer_val = v.iValue;
										if (node->op == opGT) ++timer_val;
									}
									else {
										DBG_PREDICATES << "skipping large timer value " << v.iValue << "\n";
									}
								}
								else if (tv.kind == Value::t_integer) {
									if (tv.iValue < timer_val) {
										timer_val = tv.iValue;
										if (node->op == opGT) ++timer_val;
									}
									else {
										DBG_PREDICATES << "skipping a large timer value " << tv.iValue << "\n";
									}
								}
								else {
									DBG_MSG<< _name << " Warning: timer value for state " << s.state_name << " subcondition is not numeric\n";
									continue;
								}
							}
							if (timer_val < LONG_MAX) {
								ch.timer_val = timer_val;
								DBG_M_SCHEDULER << _name << " Scheduling subcondition timer for " << timer_val*1000 << "us\n";
								ch.trigger = new Trigger("SubconditionTimer");
								Scheduler::instance()->add(new ScheduledItem(timer_val*1000, new FireTriggerAction(this, ch.trigger)));
							}

							/*
							   if (ch.timer_val.kind == Value::t_symbol) {
							   Value v = getValue(ch.timer_val.sValue);
							   if (v.kind != Value::t_integer) {
							   DBG_M_SCHEDULER << _name << " Warning: timer value for state " << s.state_name << " subcondition is not numeric\n";
							   continue;
							   }
							   else
							   timer_val = v.iValue;
							   }
							   else if (ch.timer_val.kind == Value::t_integer)
							   timer_val = ch.timer_val.iValue;
							   else {
							   DBG_M_SCHEDULER << _name << " Warning: timer value for state " << s.state_name << " subcondition is not numeric\n";
							   continue;
							   }
							   DBG_M_SCHEDULER << _name << " Scheduling subcondition timer for " << timer_val*1000 << "us\n";
							   ch.trigger = new Trigger("Timer");
							   Scheduler::instance()->add(new ScheduledItem(timer_val*1000, new FireTriggerAction(this, ch.trigger)));
							 */
						}
					}
					else if (ch.command_name == "FLAG") {
						if (s.state_name == current_state.getName()) {
							//TBD What was intended here?
						}
					}
				}
			}
		}


		if (published) {
			Channel::sendStateChange(this, new_state.getName());
		}

		/* notify everyone we are entering a state */

		std::string txt = _name + "." + new_state.getName() + "_enter";
		Message msg(txt.c_str());
		stat = execute(msg, this);
		notifyDependents(msg);
	}
	return stat;
}

void MachineInstance::notifyDependents(){
	std::set<MachineInstance*>::iterator dep_iter = depends.begin();
	while (dep_iter != depends.end()) {
		MachineInstance *dep = *dep_iter++;
		if (this == dep) continue;
		if (!dep->is_enabled) continue;
		if (!dep->is_active && !dep->io_interface) continue;
		DBG_M_MESSAGING << _name << " should tell " << dep->getName() << " to check state\n";
		Action *act = dep->executingCommand();
		if (act) {
			if (act->getStatus() == Action::Suspended) act->resume();
			DBG_M_MESSAGING << dep->getName() << "[" << dep->getCurrent().getName() << "]" << " is executing " << *act << "\n";
		}
		dep->setNeedsCheck();
//		if (dep->state_machine->token_id == ClockworkToken::LIST ) dep->notifyDependents();
	}
}

void MachineInstance::notifyDependents(Message &msg){
	std::set<MachineInstance*>::iterator dep_iter = depends.begin();
	while (dep_iter != depends.end()) {
		MachineInstance *dep = *dep_iter++;
		if (this == dep) continue;
		if (!dep->is_enabled) continue;
		if (!dep->is_active && !dep->io_interface) continue;
		DBG_M_MESSAGING << _name << " should tell " << dep->getName() 
			<< " it is " << current_state.getName() << " using " << msg << "\n";


		if (dep->receives(msg, this)) {
			if (!dep->isActive())
				dep->execute(msg, this);
			else {
				Package *p = new Package(this, dep, msg, false);
				Dispatcher::instance()->deliver(p);
			}
		}

#if 0
		Action *act = dep->executingCommand();
		if (act) {
			if (act->getStatus() == Action::Suspended) act->resume();
			DBG_M_MESSAGING << dep->getName() << "[" << dep->getCurrent().getName() 
				<< "]" << " is executing " << *act << "\n";
		}
		//TBD execute the message on the dependant machine
		if (dep->receives(msg, this)) dep->execute(msg, this);
		dep->setNeedsCheck();
//		if (dep->state_machine->token_id == ClockworkToken::LIST ) dep->notifyDependents();
#endif
	}
}


/* findHandler - find a handler for a message by looking through the transition table 

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

Action *MachineInstance::findHandler(Message&m, Transmitter *from, bool response_required) {
	std::string short_name(m.getText());
	if (short_name.find('.') != std::string::npos) {
		short_name = short_name.substr(short_name.find('.')+1);
	}
	if (from) {
		DBG_M_MESSAGING << "received message " << m << " from " << from->getName() << "\n";
	}
	if (from == this || m.getText() == short_name) {
		DBG_M_MESSAGING << _name << " checking transitions (" << transitions.size() << ")\n";
		std::list<Transition>::const_iterator iter = transitions.begin();
		while (iter != transitions.end()) {
			const Transition &t = *iter++;
			if (t.trigger.getText() == short_name && (current_state == t.source || t.source == "ANY")
					&& (!t.condition || t.condition->operator()(this))) {
				// found match, if there is a command defined for this transition, we
				// execute the command, otherwise we just do the state change
				if (commands.count(t.trigger.getText())) {
					DBG_M_MESSAGING << "Transition on machine " << getName() << " has a linked command; using it\n";
					// at the end of the transition, we expect to have moved to the designated state
					// so we ensure that happens by pushing a state change
					// but first, we check the stablestate condition for that state.

					// find state condition
					bool found = false;
					for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
						StableState &s = stable_states[ss_idx];
						if (s.state_name == t.dest.getName()) {
							DBG_M_STATECHANGES << "found stable state condition test for " << t.dest.getName() << "\n";
							if (s.subcondition_handlers) {
								std::list<ConditionHandler>::iterator iter = s.subcondition_handlers->begin();
								while (iter != s.subcondition_handlers->end()) {
									ConditionHandler&ch = *iter++;
									ch.triggered = false;
								}
							}
							MoveStateActionTemplate temp(_name.c_str(), t.dest.getName().c_str() );
							MachineCommandTemplate mc("stable_state_test", "");
							mc.setActionTemplate(&temp);
							IfCommandActionTemplate ifcat(s.condition.predicate, &mc);
							Action *a = new IfCommandAction(this, &ifcat);
							this->push(a);
							found = true;
						}
					}
					// the CALL method waits for a response once the executed command is complete
					// the response will be sent after the transition to the next state is done
					if (response_required && from) {
						DBG_M_MESSAGING << _name << " command " << t.trigger.getText() << " completion requires response\n";
						std::string response = _name + "." + t.trigger.getText() + "_done";
						ExecuteMessageActionTemplate emat(strdup(response.c_str()), strdup(from->getName().c_str()));
						ExecuteMessageAction *ema = new ExecuteMessageAction(this, emat);
						this->push(ema);
					}
					if (!found) {
						DBG_M_STATECHANGES << "no stable state condition test for " << t.dest.getName() << " pushing state change\n";
						MoveStateActionTemplate msat("SELF", t.dest.getName());
						MoveStateAction *msa = new MoveStateAction(this, msat);
						DBG_M_STATECHANGES << _name << " pushed (status: " << msa->getStatus() << ") " << *msa << "\n";
						this->push(msa);
					}
					if (commands.count(t.trigger.getText()) ) {
						// if a modbus command, make sure it is turned off again in modbus
						if (published && modbus_exports.count(_name + "." + t.trigger.getText())) {
							ModbusAddress addr = modbus_exports[_name + "." + t.trigger.getText()];
							if (addr.getSource() == ModbusAddress::command) {
								DBG_MODBUS << _name << " turning off command coil " << addr << "\n";
								addr.update(this, 0);
							}
							else {
								NB_MSG << _name << " command " << t.trigger.getText() << " is linked to an improper address\n";
							}
						}
						else {
							DBG_M_MODBUS << _name << " " << t.trigger.getText() << " is not exported to modbus, no response sent\n";
						}
						return commands[t.trigger.getText()]->retain();
					}
					else
						return NULL;
				}
				else {
					DBG_M_MESSAGING << "No linked command for the transition, performing state change\n";
				}

				// there is no matching command but a completion reply has been requested
				if (response_required && from) {
					DBG_M_MESSAGING << _name << " command " << t.trigger.getText() << " completion requires response\n";
					std::string response = _name + "." + t.trigger.getText() + "_done";
					ExecuteMessageActionTemplate emat(strdup(response.c_str()), strdup(from->getName().c_str()));
					ExecuteMessageAction *ema = new ExecuteMessageAction(this, emat);
					this->push(ema);
				}

				// no matching command, just perform the transition
				MoveStateActionTemplate temp(_name.c_str(), t.dest.getName().c_str() );
				return new MoveStateAction(this, temp);
			}
		}
		// no transition but this may still be a command
		DBG_M_MESSAGING << _name << " looking for a command with name " << short_name << "\n";
		if (commands.count(short_name)) {
			if (response_required && from) {
				DBG_M_MESSAGING << _name << " command " << short_name << " completion requires response\n";
				std::string response = _name + "." + short_name + "_done";
				ExecuteMessageActionTemplate emat(strdup(response.c_str()), strdup(from->getName().c_str()));
				ExecuteMessageAction *ema = new ExecuteMessageAction(this, emat);
				this->push(ema);
			}
			return commands[short_name]->retain();
		}
	}
	else {
		std::list<Transition>::const_iterator iter = transitions.begin();
		while (iter != transitions.end()) {
			const Transition &t = *iter++;
			if ( (t.trigger.getText() == m.getText() || t.trigger.getText() == short_name) && current_state == t.source) {
				DBG_M_MESSAGING << _name << " received message" << m.getText() << "; pushing state change\n";
				MoveStateActionTemplate msat("SELF", t.dest.getName());
				MoveStateAction *msa = new MoveStateAction(this, msat);
				DBG_M_STATECHANGES << _name << " pushed (status: " << msa->getStatus() << ") " << *msa << "\n";
				return msa;
			}
		}
	}
	// if debugging, generate a log message for the enter function
	if (debug() && _type != "POINT" && LogState::instance()->includes(DebugExtra::instance()->DEBUG_ACTIONS) ) {
		std::string message_str = _name + "." + current_state.getName() + "_enter";
		if (message_str != m.getText())
			DBG_M_MESSAGING << _name << ": No transition found to handle " << m << " from " << current_state.getName() << ". Searching receive_functions for a handler...\n";
		else
			DBG_M_MESSAGING << _name << ": Looking for ENTER method for " << current_state.getName() << "\n";
	}
	std::map<Message, MachineCommand*>::iterator receive_handler_i = receives_functions.find(Message(m.getText().c_str()));
	if (receive_handler_i != receives_functions.end()) {
		if (debug()) {
			DBG_M_MESSAGING << " found event receive handler: " << (*receive_handler_i).first << "\n"
				<< "handler: " << *((*receive_handler_i).second) << "\n";
		}
		return (*receive_handler_i).second->retain();
	}
	else if (from == this) {
		if (short_name == m.getText()) { return NULL; } // no other alternatives
		std::map<Message, MachineCommand*>::iterator receive_handler_i = receives_functions.find(Message(short_name.c_str()));
		if (receive_handler_i != receives_functions.end()) {
			if (debug()){
				DBG_M_MESSAGING << " found event receive handler: " << (*receive_handler_i).first << "\n"
					<< "handler: " << *((*receive_handler_i).second) << "\n";
			}
			return (*receive_handler_i).second->retain();
		}
	}
	return NULL;
}

void MachineInstance::collect(const Package &package) {
	if (package.transmitter) {
		if (!receives(package.message, package.transmitter)) return;
		if (package.transmitter)
			DBG_M_MESSAGING << _name << "Collecting package: " << package.transmitter->getName() << "." << *package.message << "\n";
		else
			DBG_M_MESSAGING << _name << "Collecting package: " << *package.message << "\n";
		HandleMessageActionTemplate hmat(package);
		HandleMessageAction *hma = new HandleMessageAction(this, hmat);
		Action *curr = executingCommand();
		if ( curr ) {
			curr->suspend();
			DBG_M_MESSAGING <<_name<< "Interrupted " << *curr << "\n";
		}
		DBG_M_MESSAGING << _name << " pushed " << *hma << "\n";
		active_actions.push_back(hma);
	}
}

Action::Status MachineInstance::execute(const Message&m, Transmitter *from) {
	if (!enabled()) {
#if 0
		char buf[120];
		if (from) {
			snprintf(buf, 120, "%s failed to execute %s from %s while disabled",
				_name.c_str(), m.getText().c_str(), from->getName().c_str() );
		}
		else {
			snprintf(buf, 120, "%s failed to execute %s while disabled",
				_name.c_str(), m.getText().c_str());
		}
		MessageLog::instance()->add(buf);
#endif
		return Action::Failed;
	}

	if (m.getText().length() == 0) {
		NB_MSG << _name << " error: dropping empty message\n";
		return Action::Failed;
	}

	setNeedsCheck();

	std::string event_name(m.getText());
	if (from && event_name.find('.') == std::string::npos)
		event_name = from->getName() + "." + m.getText();

	// fire the triggers on any suspended commands that might be waiting for them.
	if (executingCommand()) {
		assert(isActive());
		// tell any actions that are triggering on this that we have seen the event
		DBG_M_MESSAGING << _name << " processing triggers for " << event_name << "\n";
		std::list<Action*>::iterator iter = active_actions.begin();
		while (iter != active_actions.end()) {
			Action *a = *iter++;
			Trigger *trigger = a->getTrigger();
			if (trigger && trigger->matches(m.getText())) {
				SetStateAction *ssa = dynamic_cast<SetStateAction*>(a);
				CallMethodAction *cma = dynamic_cast<CallMethodAction*>(a);
				if (ssa) {
					if (!ssa->condition.predicate || ssa->condition(this)) {
						trigger->fire();
						DBG_M_MESSAGING << _name << " trigger on " << *a << " fired\n";
					}
					else
						DBG_M_MESSAGING << _name << " trigger message " << trigger->getName()
							<< " arrived but condition " << *(ssa->condition.predicate) << " failed\n";
				}
				// a call method action may be waiting for this message
				else if ( cma ) {
					if (cma->getTrigger()) cma->getTrigger()->fire();
					DBG_M_MESSAGING << _name << " trigger on " << *a << " fired\n";
				}
				else {
					NB_MSG << _name << " firing unknown trigger on " << *a << "\n";
					trigger->fire();
				}
			}
		}
	}
	DBG_M_MESSAGING << _name << " executing message " << m.getText()  << " from " << ( (from) ? from->getName(): "unknown" ) << "\n";

	if ( (io_interface && from == io_interface) || (mq_interface && from == mq_interface) ) {
		// for machines that are connected to hardware on/off transitions occur when the
		// machine receives an on/off_enter message from the hardware interface
		// Similarly, properties are updated in response to 'property_change' message from
		// the hardware interface

		if (!state_machine) return Action::Failed;

		if(m.getText() == "on_enter" && current_state.getName() != "on") {
			const State *on = state_machine->findState("on");
			if (on) setState(*on);
			else {
				char buf[120];
				snprintf(buf, 120, "%s does not have a state 'on' executing 'on_enter'", _name.c_str());
				MessageLog::instance()->add(buf);
				return Action::Failed;
			}
		}
		else if (m.getText() == "off_enter" && current_state.getName() != "off") {
			const State *off = state_machine->findState("off");
			if (off) setState(*off);
			else {
				char buf[120];
				snprintf(buf, 120, "%s does not have a state 'off' executing 'off_enter'", _name.c_str());
				MessageLog::instance()->add(buf);
				return Action::Failed;
			}
		}
		else if (m.getText() == "property_change" ) {
			//std::cout << _name << " value changed to " << io_interface->address.value << "\n";
			setValue("VALUE", io_interface->address.value);
		}
		// a POINT won't have an actions that depend on triggers so it's safe to return now
		return Action::Complete;
	}
	// execute the method if there is one
	DBG_M_MESSAGING << _name<< " executing " << event_name << "\n";
	HandleMessageActionTemplate hmat(Package(from, this, new Message(event_name.c_str())));
	HandleMessageAction *hma = new HandleMessageAction(this, hmat);
	Action::Status res =  (*hma)();
	hma->release();
	return res;
}

void MachineInstance::handle(const Message&m, Transmitter *from, bool send_receipt) {
	if (!enabled()) {
		DBG_M_MESSAGING << _name << ":" << id 
			<< " dropped: " << m << " in " << getCurrent() << " from " << from->getName() << " while disabled\n";
		if (send_receipt) {
			NB_MSG << _name << " Error: message '" << m.getText() << "' requiring a receipt arrived while disabled\n";
		}
		return;
	}
	if (_type != "POINT") {
		DBG_M_MESSAGING << _name << ":" << id 
			<< " received: " << m << " in " << getCurrent() << " from " << from->getName() 
			<< " will send receipt: " << send_receipt << "\n";
	}
	HandleMessageActionTemplate hmat(Package(from, this, m, send_receipt));
	HandleMessageAction *hma = new HandleMessageAction(this, hmat);
	enqueueAction(hma);
	busy_machines.insert(this);
}

void MachineInstance::sendMessageToReceiver(Message *m, Receiver *r, bool expect_reply) {
	MachineShadowInstance *msi = dynamic_cast<MachineShadowInstance*>(this);
	if (msi) {
		std::string addressed_message = fullName();
		addressed_message += ".";
		addressed_message += m->getText();
		std::list<Value> *params = new std::list<Value>;
		params->push_back(addressed_message.c_str());
		Channel::sendCommand(this, "SEND", params); // empty command parameter list
	}
	else {
		DBG_M_MESSAGING << _name << " message " << m->getText() << " expect reply: " << expect_reply << "\n";
		Package *p = new Package(this, r, m, expect_reply);
		Dispatcher::instance()->deliver(p);
	}
}

MachineClass::MachineClass(const char *class_name) : default_state("unknown"), initial_state("INIT"),
	name(class_name), allow_auto_states(true), token_id(0), plugin(0),
	polling_delay(0)
{
	states.push_back("INIT");
	token_id = Tokeniser::instance()->getTokenId(class_name);
	all_machine_classes.push_back(this);
}

void MachineClass::defaultState(State state) {
	default_state = state;
}

void MachineClass::disableAutomaticStateChanges() { 
	allow_auto_states = false; 
}

void MachineClass::enableAutomaticStateChanges() { 
	allow_auto_states = true; 
}

bool MachineClass::isStableState(State &state) {
	std::vector<StableState>::iterator iter = stable_states.begin();
	while (iter != stable_states.end()) {
		StableState &s = *iter++;
		if (s.state_name == state.getName()) return true;
	}
	return false;
}

const State *MachineClass::findState(const char *seek) const {
	std::list<State>::const_iterator iter = states.begin();
	while (iter != states.end()) {
		const State &s = *iter++;
		if (s.getName() == seek) return &s;
	}
	return 0;
}

const State *MachineClass::findState(const State &seek) const {
	std::list<State>::const_iterator iter = states.begin();
	while (iter != states.end()) {
		const State &s = *iter++;
		if (s == seek) {
			return &s;
		}
	}
	return 0;
}


MachineClass *MachineClass::find(const char *name) {
	int token = Tokeniser::instance()->getTokenId(name);
	std::list<MachineClass *>::iterator iter = all_machine_classes.begin();
	while (iter != all_machine_classes.end()) {
		MachineClass *mc = *iter++;
		if (mc->token_id == token) return mc;
	}
	return 0;
}

void MachineClass::addProperty(const char *p) {
	//NB_MSG << "Warning: ignoring OPTION " << p << " in " << name << "\n";
	property_names.insert(p);
}

void MachineClass::addCommand(const char *p) {
	//NB_MSG << "Warning: ignoring COMMAND/RECEIVES " << p << " in " << name << "\n";
	command_names.insert(p);
}


MachineEvent::MachineEvent(MachineInstance *machine, Message *message) :
	 mi(machine), msg(message) {}

MachineEvent::MachineEvent(MachineInstance *machine, const Message &message) :
	mi(machine), msg(new Message(message)) { }

MachineEvent::~MachineEvent() { delete msg; }

MachineInterface::MachineInterface(const char *class_name) : MachineClass(class_name) {
	if (all_interfaces.count(class_name) != 0) {
		NB_MSG << "interface " << class_name << " is already defined\n";
	}
	all_interfaces[class_name] = this;
}

MachineInterface::~MachineInterface() {
	all_interfaces.erase(this->name);
}
/*
void MachineInterface::addProperty(const char *name) {
	property_names.insert(name);
}

void MachineInterface::addCommand(const char *name) {
	command_names.insert(name);
}
*/


Action *MachineInstance::executingCommand() {
	if (!active_actions.empty()) return active_actions.back();
	return 0;
}

void MachineInstance::start(Action *a) {
	if (!active_actions.empty()) {
		Action *b = executingCommand();
		if (b && b->isBlocked() && b->blocker() != a) {
			DBG_M_ACTIONS << "WARNING: "<<_name << " is executing command " <<*b <<" when starting " << *a << "\n";
		}
		if (b == a) {
			std::stringstream ss;
			ss << owner->fullName() << " Failed to start action: " << *a << " already running\n";
			MessageLog::instance()->add(ss.str().c_str());
			NB_MSG << ss.str() << "\n";
			return;
		}
	}
	DBG_M_ACTIONS << _name << " pushing (state: " << a->getStatus() << ")" << *a << "\n";
	if (tracing() && isTraceable()) {
		resetTemporaryStringStream();
		ss << "starting action: " << *a;
		setValue("TRACE", ss.str());
	}
	//    DBG_MSG << "STARTING: " << *a << "\n";
	active_actions.push_back(a->retain());
}

void MachineInstance::displayActive(std::ostream &note) {
	if (active_actions.empty()) return;
	note << _name << ':' << id << " stack:\n";
	const char *delim = "";
	BOOST_FOREACH(Action *act, active_actions) {
		note << delim << " " << *act << " (status:" << act->getStatus() << ") ";
		delim = "\n";
	}
}

// stop removes the action
void MachineInstance::stop(Action *a) {
	//	if (active_actions.back() != a) {
	//		DBG_M_ACTIONS << _name << "Top of action stack is no longer " << *a << "\n";
	//		return;
	//	}
	//	active_actions.pop_back();
	//    a->release();

	bool found = false;
	std::list<Action*>::iterator iter = active_actions.begin();
	while (iter != active_actions.end()) {
		Action *queued = *iter;
		if (queued == a) {
			if (found)
				std::cerr << "Warning: action " << *a << " was queued twice\n";
			iter = active_actions.erase(iter);
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
			if (next->suspended()) next->resume();
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
		if (a->getTrigger() && a->getTrigger()->enabled() && !a->getTrigger()->fired() )
			a->disableTrigger();
		a->release();
	}

	boost::mutex::scoped_lock(q_mutex);
	mail_queue.clear();
	has_work = false;
}

void MachineInstance::markActive() {
	is_active = true;
	active_machines.remove(this);
	active_machines.push_back(this);
	if (state_machine && state_machine->plugin) markPlugin();
}

void MachineInstance::markPassive() {
	is_active = false;
	active_machines.remove(this);
}

void MachineInstance::markPlugin() {
	plugin_machines.insert(this);
}

void MachineInstance::resume() {
	if (!is_enabled) {
		// adjust the starttime of the current state to make allowance for the 
		// time we were disabled
		struct timeval now;
		gettimeofday(&now, 0);
		long dt = (long)get_diff_in_microsecs(&now, &disabled_time);
		start_time.tv_usec += dt % 1000000L;
		start_time.tv_sec += dt / 1000000L;
		if (start_time.tv_usec >= 1000000L) {
			start_time.tv_usec -= 1000000L;
			++start_time.tv_sec;
		}

		// fix status
		is_enabled = true; 
		error_state = 0;
		for (unsigned int i = 0; i<locals.size(); ++i) {
			locals[i].machine->resume();
		}
		setState(current_state, true);
		setNeedsCheck();
	} 
}

void fixListState(MachineInstance &list) {
	const State *s = 0;
	if (list.parameters.empty())
		s = list.getStateMachine()->findState("empty");
	else
		s = list.getStateMachine()->findState("nonempty");
	assert(s);
	list.setState(*s);
}

void MachineInstance::setInitialState() {
	if (io_interface) {
		io_interface->setInitialState();
	}
	if (state_machine) {
		if (io_interface) {
			const State *s = state_machine->findState(io_interface->getStateString());
			if (s) setState(*s);
			else {
				char buf[100];
				snprintf(buf, 100,"Warning: setting initial state on %s to unknown state: %s\n", _name.c_str(), io_interface->getStateString());
				MessageLog::instance()->add(buf);
				NB_MSG << buf << "\n";
			}
		}
		else {
			if (state_machine->token_id == ClockworkToken::LIST )
				fixListState(*this);
			else
				setState(state_machine->initial_state); 
			setNeedsCheck();
		}
	}
	else {
		char buf[100];
		snprintf(buf, 100,"Warning: setting initial state on %s when it has no state machine\n", _name.c_str());
		MessageLog::instance()->add(buf);
		NB_MSG << buf << "\n";
	}
} 

void MachineInstance::publish() { 
	++published; 
}
void MachineInstance::unpublish() { --published; }

void MachineInstance::enable() {
	if (is_enabled) return;
	is_enabled = true; 
	error_state = 0; 
	clearAllActions(); 
	if (io_interface) io_interface->handleChange(pending_events);
	for (unsigned int i = 0; i<locals.size(); ++i) {
		if (!locals[i].machine) {
			std::stringstream ss;
			ss << "No machine found for " << locals[i].val << " in " << _name;
			char *msg = strdup(ss.str().c_str());
			MessageLog::instance()->add(msg);
			NB_MSG << msg << "\n";
			free(msg);
		}
		else
			locals[i].machine->enable();
	}
	if (_type == "LIST") // enabling a list enables the members
		for (unsigned int i = 0; i<parameters.size(); ++i) {
			// parameters my be just values but if they are machines they need to be enabled
			if (parameters[i].machine) parameters[i].machine->enable();
		}

	setInitialState();
	setNeedsCheck();
	// if any dependent machines are already enabled, make sure they know we are awake
	std::set<MachineInstance *>::iterator d_iter = depends.begin();
	while (d_iter != depends.end()) {
		MachineInstance *dep = *d_iter++;
		if (dep->enabled()) dep->setNeedsCheck(); // make sure dependant machines update when a property changes
	}	
}

void MachineInstance::setDebug(bool which) {
	allow_debug = which;
	for (unsigned int i = 0; i<locals.size(); ++i) {
		locals[i].machine->setDebug(which);
	}
}

void MachineInstance::disable() { 
	is_enabled = false;
	if (locked) locked = 0;
	if (io_interface) {
		io_interface->turnOff();
	}
	
	Value &val = properties.lookup("default");
	if (val != SymbolTable::Null) {
		if (val.kind == Value::t_integer) {
			std::cout << "Initialising value for " << _name << " to " << val << "\n";
			long i_val = 0;
			if (val.asInteger(i_val)) {
				setValue("VALUE", i_val);
				if (io_interface) {
					if (io_interface->address.is_signed)
						io_interface->setValue((int32_t)(i_val & 0xffffffff));
					else
						io_interface->setValue((uint32_t)(i_val & 0xffffffff));
				}
			}
		}
	}

	gettimeofday(&disabled_time, 0);
	if (_type == "LIST") // disabling a list disables the members
		for (unsigned int i = 0; i<parameters.size(); ++i) {
			if (parameters[i].machine) parameters[i].machine->disable();
		}
	for (unsigned int i = 0; i<locals.size(); ++i) {
		locals[i].machine->disable();
	}
	// if any dependent machines are already enabled, make sure they know we are disabled
	std::set<MachineInstance *>::iterator d_iter = depends.begin();
	while (d_iter != depends.end()) {
		MachineInstance *dep = *d_iter++;
		if (dep->enabled())dep->setNeedsCheck(); // make sure dependant machines update when a property changes
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
		for (unsigned int i = 0; i<locals.size(); ++i) {
			locals[i].machine->resume();
		}
		setState(s, true);
		setNeedsCheck();
	}
}

void MachineInstance::resumeAll() {
	// resume all local machines that have not already been enabled
	for (unsigned int i = 0; i<locals.size(); ++i) {
		if (!locals[i].machine->enabled())
			locals[i].machine->resume();
	}
}


void MachineInstance::push(Action *new_action) {
	if (!active_actions.empty()) {
		active_actions.back()->suspend();
		active_actions.back()->setBlocker(new_action);
	}
	DBG_M_ACTIONS << _name << " pushing " << *new_action << "\n";
	active_actions.push_back(new_action);
	num_machines_with_work++;
	has_work = true;
	busy_machines.insert(this);
	DBG_M_ACTIONS << _name << " ADDED to machines with work " << busy_machines.size() << "\n";
	return;
}

void MachineInstance::updateLastEvaluationTime() {
	struct timeval now;
	gettimeofday(&now, NULL);
	last_state_evaluation_time = now.tv_sec * 1000000 + now.tv_usec;
	if (idle_time)
		next_poll = last_state_evaluation_time + idle_time;
	else {
		if (state_machine && state_machine->polling_delay)
			next_poll = last_state_evaluation_time + state_machine->polling_delay;
		else
			next_poll = last_state_evaluation_time + MachineInstance::polling_delay->iValue;
	}
}

bool MachineInstance::setStableState() {
	bool changed_state = false;
	CaptureDuration cd(stable_states_stats);
	DBG_M_AUTOSTATES << _name << " checking stable states (currently " << current_state.getName()  <<")\n";
	if (!state_machine || !state_machine->allow_auto_states) {
		DBG_M_AUTOSTATES << _name << " aborting stable states check due to configuration\n";
		return false;
	}
	if ( executingCommand() || !mail_queue.empty() ) {
		DBG_M_AUTOSTATES << _name << " aborting stable states check due to command execution\n";
		return false;
	}
	// we must not set our stable state if objects we depend on are still updating their own state
	needs_check = 0;
	updateLastEvaluationTime();

	if (io_interface) {
		const char *io_state_name = io_interface->getStateString();
		const State *s = state_machine->findState(io_state_name);
		if (s) setState(*s);
		else {
			char buf[200];
			snprintf(buf, 200,"Warning: %s does not have a state %s to match the hardware\n", _name.c_str(), io_state_name);
			MessageLog::instance()->add(buf);
			NB_MSG << buf << "\n";
		}
	}
	else if ( ( !state_machine && _type=="LIST") || (state_machine && state_machine->token_id == ClockworkToken::LIST) ) {
		//DBG_MSG << _name << " has " << parameters.size() << " parameters\n";
		fixListState(*this);
	}
	else if (( !state_machine && _type=="REFERENCE") || (state_machine && state_machine->token_id == ClockworkToken::REFERENCE) ) {
		//DBG_MSG << _name << " has " << parameters.size() << " parameters\n";
		if (locals.size()) {
			const State *s = state_machine->findState("ASSIGNED");
			assert(s);
			setState(*s);
		}
		else{
			const State *s = state_machine->findState("EMPTY");
			assert(s);
			setState(*s);
		}
	}
	else {
		bool found_match = false;
		const long MAX_TIMER = 100000000L;
		long next_timer = MAX_TIMER; // during the search, we find the shortest timer value and schedule a wake-up
		for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
			StableState &s = stable_states[ss_idx];
			// the following test should be enabled but there is currently a situation that 
			// can cause the stable state to not be evaluated when a trigger is set. TBD
			//		if ( (s.trigger && s.trigger->enabled() && s.trigger->fired() && s.condition(this) )
			//			|| (!s.trigger && s.condition(this)) ) {
			if (!found_match) {
				if (s.condition(this)) {
					DBG_M_PREDICATES << _name << "." << s.state_name <<" condition " << *s.condition.predicate << " returned true\n";
					if (current_state.getName() != s.state_name) {
						DBG_M_AUTOSTATES << " changing state\n";
						changed_state = true;
						if (tracing() && isTraceable()) {
							resetTemporaryStringStream();
							ss << current_state.getName() <<"->" << s.state_name << " " << *s.condition.predicate;
							setValue("TRACE", ss.str());
						}
						if (s.subcondition_handlers) {
							std::list<ConditionHandler>::iterator iter = s.subcondition_handlers->begin();
							while (iter != s.subcondition_handlers->end()) {
								ConditionHandler&ch = *iter++;
								ch.triggered = false;
							}
						}
						DBG_AUTOSTATES << _name << ":" << id << " (" << current_state << ") should be in state " << s.state_name 
							<< " due to condition: " << *s.condition.predicate << "\n";
						char *sn = strdup(s.state_name.c_str());
						MoveStateActionTemplate temp(_name.c_str(), sn );
						state_change = new MoveStateAction(this, temp);
						Action::Status action_status;
						if ( (action_status = (*state_change)()) == Action::Failed) {
							DBG_MSG << " Warning: failed to start moving state on " << _name << " to " << s.state_name<< "\n";
						}
						else {
							DBG_AUTOSTATES << " started state change on " << _name << " to " << s.state_name<<"\n";
						}
						free(sn);
						//if (action_status == Action::Complete || action_status == Action::Failed) {
						state_change->release();
						state_change = 0;
					}
					else {
						DBG_AUTOSTATES << " already there\n";
						// reschedule timer triggers for this state
						if (s.uses_timer) {
							DBG_SCHEDULER << _name << " should retrigger timer for " << s.state_name << "("<<s.timer_val<< ")"<< "\n";
							Value v = getValue(s.timer_val.sValue);
							if (v.kind == Value::t_integer && v.iValue < next_timer)
								next_timer = v.iValue;
						}
					}
					if (s.uses_timer) {
						DBG_SCHEDULER << _name << "[" << current_state.getName() 
							<< "] scheduling condition tests for state " << s.state_name << "\n";
						s.condition.predicate->scheduleTimerEvents(this);
					}
					if (s.subcondition_handlers) {
						std::list<ConditionHandler>::iterator iter = s.subcondition_handlers->begin();
						while (iter != s.subcondition_handlers->end()) {
							ConditionHandler *ch = &(*iter++);
							ch->check(this);
						}
					}
					found_match = true;
					continue; // skip to the end of the stable state loop
				}
				else {
					DBG_PREDICATES << _name << " " << s.state_name << " condition " << *s.condition.predicate << " returned false\n";
					if (s.uses_timer) {
						DBG_SCHEDULER << _name  << "[" << current_state.getName() 
							<< "] scheduling condition tests for state " << s.state_name << "\n";
						s.condition.predicate->scheduleTimerEvents(this);
						//Value v = s.timer_val;
						// there is a bug here; if the timer used in this state is on a different machine
						// the schedule we generate has to be based on the timer of the other machine.
						//if (v.kind == Value::t_integer && v.iValue < next_timer) {
						// we would like to say  next_timer = v.iValue; here but since we have already been in this
						// state for some time we need to say:
						//    Value current_timer = *getTimerVal();
						//    next_timer = v.iValue - current_timer.iValue;
						//}
					}
				}

			}
			if (next_timer < MAX_TIMER) {
				/*
				   If only we could now just schedule a timer event:
				   Trigger *trigger = new Trigger("Timer");
				   Scheduler::instance()->add(new ScheduledItem(next_timer*1000, new FireTriggerAction(owner, trigger)));
				   unfortunately, the current code still has a bug because the 'uses_timer' flag of stable states
				   simply indicates that the state depends on *something* that uses a timer but that may be another
				   machine and that machine may not be in the state we are interested in.

				   Workaround for now: check stable states every millisecond or so if any state uses a timer.
				 */
				/*  Trigger *trigger = new Trigger("Timer");
				    Scheduler::instance()->add(new ScheduledItem(1000, new FireTriggerAction(this, trigger)));
				    trigger->release(); */
			}
			// this state is not active so ensure its subcondition flags are turned off
			if (s.subcondition_handlers) {
				std::list<ConditionHandler>::iterator iter = s.subcondition_handlers->begin();
				while (iter != s.subcondition_handlers->end()) {
					ConditionHandler&ch = *iter++;
					if (ch.command_name == "FLAG" ) {
						MachineInstance *flag = lookup(ch.flag_name);
						if (flag) {
							const State *s = flag->state_machine->findState("off");
							flag->setState(*s);
						}
						else
							std::cerr << _name << " error: flag " << ch.flag_name << " not found\n"; 
					}
				}
			}
		}
	}
#if 0
	if (!needs_check) { BOOST_FOREACH(Transmitter *t, listens) {
		MachineInstance *m = dynamic_cast<MachineInstance *>(t);
		if (m && m!= this && m->state_machine && m->state_machine-> allow_auto_states && m->needs_check) {
			DBG_M_AUTOSTATES << _name << " waiting for " << m->getName() << " to stablize \n";
			//needs_check = true;
			return;
		}
	}}
#endif
	return changed_state;
	}

void MachineInstance::setStateMachine(MachineClass *machine_class) {
	//if (state_machine) return;
	state_machine = machine_class;
	DBG_PARSER << _name << " is of class " << machine_class->name << "\n";
	if (my_instance_type == MACHINE_INSTANCE && machine_class->allow_auto_states
			&& (machine_class->stable_states.size() || machine_class->name == "LIST" || machine_class->name == "REFERENCE"
				|| (machine_class->plugin && machine_class->plugin->state_check)) )
		automatic_machines.push_back(this);
	BOOST_FOREACH(StableState &s, machine_class->stable_states) {
		stable_states.push_back(s);
		stable_states[stable_states.size()-1].setOwner(this); //
	}
	std::pair<Message, MachineCommandTemplate*>handler;
	BOOST_FOREACH(handler, machine_class->receives) {
		//DBG_M_INITIALISATION << "setting receive handler for " << handler.first << "\n";
		MachineCommand *mc = new MachineCommand(this, handler.second);
		//mc->retain();
		receives_functions[handler.first] = mc;
	}
	// Warning: enter_functions are not used. TBD
	BOOST_FOREACH(handler, machine_class->enter_functions) {
		MachineCommand *mc = new MachineCommand(this, handler.second);
		mc->retain();
		enter_functions[handler.first] = mc;
	}
	std::pair<std::string, MachineCommandTemplate*> node;
	BOOST_FOREACH(node, state_machine->commands) {
		MachineCommand *mc = new MachineCommand(this, node.second);
		mc->retain();
		commands[node.first] = mc;
	}
	std::pair<std::string,Value> option;
	BOOST_FOREACH(option, machine_class->options) {
		DBG_INITIALISATION << _name << " initialising property " << option.first << " (" << option.second << ")\n";
		if (option.second.kind == Value::t_symbol) {
			Value v = getValue(option.second.sValue);
			if (v != SymbolTable::Null) 
				properties.add(option.first, v, SymbolTable::NO_REPLACE);
			else
				properties.add(option.first, option.second, SymbolTable::NO_REPLACE);
		}
		else {
			properties.add(option.first, option.second, SymbolTable::NO_REPLACE);
		}
		if (option.first == "POLLING_DELAY") {
			long pd;
			if (option.second.asInteger(pd)) idle_time = pd;
		}
	}
	properties.add("NAME", _name.c_str(), SymbolTable::ST_REPLACE);
	if (locals.size() == 0) {
		BOOST_FOREACH(Parameter p, state_machine->locals) {
			DBG_MSG << "cloning machine '" << p.val.sValue << "'\n";
			Parameter newp(p.val.sValue.c_str());
			if (!p.machine) {
				DBG_M_INITIALISATION << "failed to clone. no instance defined for parameter " << p.val.sValue << "\n";
			}
			else {
				newp.machine = MachineInstanceFactory::create(p.val.sValue.c_str(), p.machine->_type.c_str());
				newp.machine->setProperties(p.machine->properties);
				newp.machine->setDefinitionLocation(p.machine->definition_file.c_str(), p.machine->definition_line);
				listenTo(newp.machine);
				newp.machine->addDependancy(this);
				std::map<std::string, MachineClass*>::iterator c_iter = machine_classes.find(newp.machine->_type);
				if (c_iter == machine_classes.end()) 
					DBG_M_INITIALISATION <<"Warning: class " << newp.machine->_type << " not found\n"
						;
				else if ((*c_iter).second) {
					newp.machine->owner = this;
					MachineClass *newsm = (*c_iter).second;
					newp.machine->setStateMachine(newsm);
					if (newsm->parameters.size() != p.machine->parameters.size()) {
						// LISTS can have any number of parameters
						// POINTs and ANALOGINPUTs can have 2 or three parameters
						if (newsm->name == "LIST") {
							DBG_PARSER << "List has " << p.machine->parameters.size() << " parameters\n";
							p.machine->setNeedsCheck();
						}
						if (newsm->name == "LIST"
								|| ( (newsm->name == "POINT" || newsm->name == "ANALOGINPUT" || newsm->name == "COUNTER")
									&& newsm->parameters.size() >= 2 && newsm->parameters.size() <=3 )
								|| ( newsm->name == "COUNTERRATE" && (newsm->parameters.size() == 3 || newsm->parameters.size() == 1) )
						   ) {
						}
						else {
							resetTemporaryStringStream();
							ss << "## - Error: Machine " << newsm->name << " requires " 
								<< newsm->parameters.size()
								<< " parameters but instance " << _name << "." << newp.machine->getName() << " has " << p.machine->parameters.size();
							error_messages.push_back(ss.str());
							++num_errors;
						}
					}
					if (p.machine->parameters.size()) {
						std::copy(p.machine->parameters.begin(), p.machine->parameters.end(), back_inserter(newp.machine->parameters));
						DBG_M_INITIALISATION << "copied " << p.machine->parameters.size() << " parameters. local has " << p.machine->parameters.size() << "parameters\n";
					}
					if (p.machine->stable_states.size()) {
						DBG_M_INITIALISATION << " restoring stable states for " << newp.val << "...before: " << newp.machine->stable_states.size();
						newp.machine->stable_states.clear();
						BOOST_FOREACH(StableState &s, p.machine->stable_states) {
							newp.machine->stable_states.push_back(s);
							newp.machine->stable_states[newp.machine->stable_states.size()-1].setOwner(this); //
						}
						std::copy(p.machine->stable_states.begin(), p.machine->stable_states.end(), back_inserter(newp.machine->stable_states));
						DBG_M_INITIALISATION << " after: " << newp.machine->stable_states.size() << "\n";
					}
				}
			}
			locals.push_back(newp);
		}
	}
	// a list has many parameters but the list class does not.
	// nevertheless, the parameters and dependencies still need to be setup.
	if (state_machine->token_id == ClockworkToken::LIST) {
		for (unsigned int i=0; i<parameters.size(); ++i) {
			parameters[i].real_name = parameters[i].val.sValue;
			MachineInstance *m = lookup(parameters[i].real_name);
			if(m) {
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
	for (unsigned int i=0; i<parameters.size(); ++i) {
		if (i<num_class_params) {
			if (parameters[i].val.kind == Value::t_symbol) {
				parameters[i].real_name = parameters[i].val.sValue;
				parameters[i].val = state_machine->parameters[i].val.sValue.c_str();
				MachineInstance *m = lookup(parameters[i].real_name);
				DBG_M_INITIALISATION << _name << " is looking up " << parameters[i].real_name << " for param " << i << "\n";
				if(m) { 
					DBG_M_INITIALISATION << " found " << m->getName() << "\n"; 
					m->addDependancy(this);
					listenTo(m);
				}
				parameters[i].machine = m;
			}
			else {
				DBG_M_MSG << "Parameter " << i << " (" << parameters[i].val << ") with real name " <<  state_machine->parameters[i].val << "\n";
			}
			// fix the properties for the machine passed as a parameter
			// the statemachine may nominate a default property that isn't 
			// provided by the actual parameter. here we find these situations
			// and set the default
			if (parameters[i].machine && !state_machine->parameters[i].properties.empty()) {
				SymbolTableConstIterator st_iter = state_machine->parameters[i].properties.begin();
				while(st_iter != state_machine->parameters[i].properties.end()) {
					std::pair<std::string, Value> prop = *st_iter++;
					if (!parameters[i].machine->properties.exists(prop.first.c_str())) {
						DBG_M_INITIALISATION << "copying default property " << prop.first << " on parameter " << i ;
						DBG_M_INITIALISATION << " to object" << *(parameters[i].machine) <<"\n";
						parameters[i].machine->properties.add(prop.first, prop.second, SymbolTable::NO_REPLACE);
					}
					else {
						DBG_M_INITIALISATION << "default property " << prop.first << " overridden by value " ;
						DBG_M_INITIALISATION << parameters[i].machine->properties.lookup(prop.first.c_str()) << "\n";
					}
				}
			}
		}
	}
	// copy transitions to support transtions on receipt of events from other machines
	BOOST_FOREACH(Transition &transition, state_machine->transitions) {
		transitions.push_back(transition);

		// copy this transition with the real name of the machine for the trigger
		std::string machine = transition.trigger.getText();
		if (machine.find('.') != std::string::npos) {
			machine.erase((machine.find('.')));
			// if this is a parameter, copy the transition to use the real name
			for (unsigned int i = 0; i < state_machine->parameters.size(); ++i) {
				if (parameters[i].val == machine) {
					std::string evt = transition.trigger.getText();
					evt = evt.substr(evt.find('.')+1);
					std::string trigger = parameters[i].real_name + "." + evt;
					transitions.push_back(Transition(transition.source, transition.dest,Message(trigger.c_str())));
				}
			}
		}
	}

	setupModbusInterface();
}

std::ostream &operator<<(std::ostream &out, const Action &a) {
	return a.operator<<(out);
}


Trigger *MachineInstance::setupTrigger(const std::string &machine_name, const std::string &message, const char *suffix = "_enter") {
	std::string trigger_name = machine_name;
	trigger_name += ".";
	trigger_name += message;
	trigger_name += suffix;
	DBG_M_MESSAGING << _name << " is waiting for message " << trigger_name << " from " << machine_name << "\n";
	return new Trigger(trigger_name);
}

Value *MachineInstance::resolve(std::string property) {
	if (property.find('.') != std::string::npos) {
		// property is on another machine
		std::string name = property;
		name.erase(name.find('.'));
		std::string prop = property.substr(property.find('.')+1 );
		MachineInstance *other = lookup(name);
		if (other) {
			return other->resolve(prop);
		}
		else if (state_machine->token_id == ClockworkToken::REFERENCE && name == "ITEM" && locals.size() == 0) {
			// permit references items to be not always available so that these can be
			// added and removed as the program executes
			return &SymbolTable::Null;
		}
		else {
			resetTemporaryStringStream();
			ss << fullName() << " resolve() could not find machine named " << name << " for property " << property;
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
		Value *res = 0;
		Value property_val(property); // tokenise the property
		// variables may refer to an initialisation value passed in as a parameter.
		if (state_machine->token_id == ClockworkToken::VARIABLE
				|| state_machine->token_id == ClockworkToken::CONSTANT) {
			for (unsigned int i=0; i<parameters.size(); ++i) {
				if (state_machine->parameters[i].val.kind == Value::t_symbol
						&& property == state_machine->parameters[i].val.sValue
				   ) {
					DBG_M_PREDICATES << _name << " found parameter " << i << " to resolve " << property << "\n";
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
				Value *global_property_val = &m->properties.lookup("VALUE");
				if (*global_property_val == SymbolTable::Null) {
					DBG_M_PROPERTIES << _name << " using current state from global " << m->fullName() << "\n";
					return &m->current_state_val;
				}
				else {
					DBG_M_PROPERTIES << _name << " using value property " << m->getValue("VALUE") << " from global " << m->fullName() << "\n";
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
		DBG_M_PROPERTIES << getName() << " looking up property " << property << "\n";
		if (property_val.token_id == ClockworkToken::TIMER) {
			// we do not use the precalculated timer here since this may be being accessed
			// within an action handler of a nother machine and will not have been updated
			// since the last evaluation of stable states.
			state_timer.dynamicValue()->operator()(this);
			DBG_M_PROPERTIES << getName() << " timer: " << state_timer << "\n";
			return &state_timer;
		}
		else if ( (res = lookupState(property_val)) != &SymbolTable::Null ) {
			return res;
		}
		else if (SymbolTable::isKeyword(property_val)) {
			return &SymbolTable::getKeyValue(property.c_str());
		}
		else {
			Value *res = &properties.lookup(property.c_str());
			if (*res == SymbolTable::Null) {
				if (state_machine) {
					Value *class_property = &state_machine->properties.lookup(property.c_str());
					if (class_property == &SymbolTable::Null) {
						DBG_M_PROPERTIES << "no property " << property << " found in class, looking in globals\n";
						if (globals.exists(property.c_str()))
							return &globals.lookup(property.c_str());
					}
				}
			}
			else {
				DBG_M_PROPERTIES << "found property " << property << " " << res->asString() << "\n";
				return res;
			}
		}

		// finally, the 'property' may be a VARIABLE or CONSTANT machine declared locally
		MachineInstance *m = lookup(property);
		if (m) {
			if ( m->state_machine && (m->state_machine->token_id == ClockworkToken::VARIABLE
						|| m->state_machine->token_id == ClockworkToken::CONSTANT) ) {
				return &m->properties.lookup("VALUE");
			}
			else if (m->getStateMachine()) {

				if (m->getStateMachine()->token_id == ClockworkToken::VARIABLE || m->getStateMachine()->token_id == ClockworkToken::CONSTANT) {
					return &m->properties.lookup("VALUE");
				}
				else if (m->getStateMachine()->token_id == ClockworkToken::LIST || m->getStateMachine()->token_id == ClockworkToken::REFERENCE) {
					return m->getCurrent().getNameValue();
				}
			}
			//return new Value(new MachineValue(m, property));
			return &m->current_value_holder;
		}
	}
	char buf[200];
	snprintf(buf,200,"%s: no such property or machine: %s",fullName().c_str(), property.c_str());
	MessageLog::instance()->add(buf);
	NB_MSG << buf << "\n";
	return &SymbolTable::Null;
}

Value *MachineInstance::getValuePtr(Value &property) {
	if (property.cached_value) return property.cached_value;
	assert(property.kind == Value::t_symbol || property.kind == Value::t_string);
	Value &res = getValue(property.sValue);
	property.cached_value = &res;
	return property.cached_value;
}

Value &MachineInstance::getValue(Value &property) {
	if (property.cached_value) return *property.cached_value;
	assert(property.kind == Value::t_symbol || property.kind == Value::t_string);
	Value &res = getValue(property.sValue);
	property.cached_value = &res;
	return res;
}

Value &MachineInstance::getValue(std::string property) {
	if (property.find('.') != std::string::npos) {
		// property is on another machine
		std::string name = property;
		name.erase(name.find('.'));
		std::string prop = property.substr(property.find('.')+1 );
		MachineInstance *other = lookup(name);
		if (other) {
			Value &v = other->getValue(prop);
			DBG_M_PROPERTIES << other->getName() << " found property " << prop << " in machine " << name << " with value " << v << "\n";
			return v;
		}
		else if (state_machine->token_id == ClockworkToken::REFERENCE && name == "ITEM" && locals.size() == 0) {
			// permit references items to be not always available so that these can be
			// added and removed as the program executes
			return SymbolTable::Null;
		}
		else {
			resetTemporaryStringStream();
			ss << " getValue() could not find machine named " << name << " for property " << property;
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
		if ( state_machine
			 && (state_machine->token_id == ClockworkToken::VARIABLE
				|| state_machine->token_id == ClockworkToken::CONSTANT)
			) {
			for (unsigned int i=0; i<parameters.size(); ++i) {
				if (state_machine->parameters[i].val.kind == Value::t_symbol 
						&& property == state_machine->parameters[i].val.sValue

				   ) {
					DBG_M_PREDICATES << _name << " found parameter " << i << " to resolve " << property << "\n";
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
				DBG_M_PROPERTIES << _name << " using value property " << m->getValue("VALUE") << " from " << m->getName() << "\n";
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
		DBG_M_PROPERTIES << getName() << " looking up property " << property << "\n";
		if (property_val.token_id == ClockworkToken::TIMER) {
			// we do not use the precalculated timer here since this may be being accessed
			// within an action handler of a nother machine and will not have been updated
			// since the last evaluation of stable states.
			getTimerVal();
			DBG_M_PROPERTIES << getName() << " timer: " << state_timer << "\n";
			return state_timer;
		}
		else if (SymbolTable::isKeyword(property.c_str())) {
			return SymbolTable::getKeyValue(property.c_str());
		}
		else {
			Value &x = properties.lookup(property.c_str());
			if (x == SymbolTable::Null) {
				if (state_machine) {
					if (!state_machine->properties.exists(property.c_str())) {
						DBG_M_PROPERTIES << "no property " << property << " found in class, looking in globals\n";
						if (globals.exists(property.c_str()))
							return globals.lookup(property.c_str());
					}
					else {
						DBG_M_PROPERTIES << "using property " << property << "from class\n";
						return state_machine->properties.lookup(property.c_str()); 
					}
				}
			}
			else {
				DBG_M_PROPERTIES << "found property " << property << " " << x.asString() << "\n";
				return x;
			}
		}

		// finally, the 'property' may be a VARIABLE or CONSTANT machine declared locally
		MachineInstance *m = lookup(property);
		if (m) {
			if (m->state_machine->token_id == ClockworkToken::VARIABLE
					|| m->state_machine->token_id == ClockworkToken::CONSTANT)
				return m->getValue("VALUE");
			else
				return *m->getCurrentStateVal();
		}

	}
	return SymbolTable::Null;
}

Value *MachineInstance::lookupState(const std::string &state_name) {
	//is state_name a valid state?
	if (state_machine) {
		std::list<State>::iterator iter = state_machine->states.begin();
		while (iter != state_machine->states.end()) {
			State &s = *iter++;
			if (s.getName() == state_name) {
				return s.getNameValue();
			}
		}
		for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
			if (stable_states[ss_idx].state_name == state_name) return &stable_states[ss_idx].name;
		}
	}
	return &SymbolTable::Null;
}

Value *MachineInstance::lookupState(const Value &state_name) {
	//is state_name a valid state?
	if (state_machine) {
		std::list<State>::iterator iter = state_machine->states.begin();
		while (iter != state_machine->states.end()) {
			State &s = *iter++;
			if (s.getId() == state_name.token_id) {
				return s.getNameValue();
			}
		}
		for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
			if (stable_states[ss_idx].name == state_name) return &stable_states[ss_idx].name;
		}
	}
	return &SymbolTable::Null;
}

bool MachineInstance::hasState(const State &test) const {
	if (state_machine) {
		const State *s = state_machine->findState(test);
		if (s) return true;

		for (unsigned int ss_idx = 0; ss_idx < stable_states.size(); ++ss_idx) {
			if (stable_states[ss_idx].state_name == test.getName()) {
				// TBD remove this code
				//all stable states should have been registered in the machines state table
				// this test is to verify that
				assert(false);
				return true;
			}
		}
	}
	else {
		char buf[100];
		snprintf(buf,100,"warning: %s does not have a state; %s", fullName().c_str(), test.getName().c_str());
		MessageLog::instance()->add(buf);
	}
	return false;
}

bool MachineInstance::hasState(const std::string &state_name) const {
	//is state_name a valid state?
	State s(state_name.c_str());
	return hasState(s);
}

void MachineInstance::sendModbusUpdate(const std::string &property_name, const Value &new_value) {
//NB_MSG << _name << " Sending modbus update " << property_name  << " " << new_value << "\n";

	ModbusAddress ma = modbus_exports[property_name];
	switch(ma.getGroup()) {
		case ModbusAddress::none:
			DBG_M_MODBUS << property_name << " export type is 'none'\n";
			break;
		case ModbusAddress::discrete:
		case ModbusAddress::coil:
		case ModbusAddress::input_register:
		case ModbusAddress::holding_register: {
			if (new_value.kind == Value::t_integer) {
				long intVal;
				if (ma.length() == 1 || ma.length() == 2) {
					if (new_value.asInteger(intVal)) {
						ma.update(this, (int)intVal);
					}
					else {
						DBG_M_MODBUS << property_name << " does not have an integer value\n";
					}
				}
			}
			else if (new_value.kind == Value::t_string || new_value.kind == Value::t_symbol){
				ma.update(this, new_value.sValue);
			}
			else {
				DBG_M_MODBUS << "unable to export " << property_name << "\n";
			}
		}
			break;
		case ModbusAddress::string: {
			ma.update(this, new_value.asString());
		}
	}
}

void MachineInstance::setValue(const std::string &property, Value new_value) {
	//forceStableStateCheck();
	//forceIdleCheck();
	DBG_M_PROPERTIES << _name << " setvalue " << property << " to " << new_value << "\n";
	if (property.find('.') != std::string::npos) {
		// property is on another machine
		std::string name = property;
		name.erase(name.find('.'));
		std::string prop = property.substr(property.find('.')+1 );
		MachineInstance *other = lookup(name);
		if (other) {
			if (prop.length()) {
				DBG_M_PROPERTIES << *other << " setting property " << prop << " to " << new_value << "\n";
				other->setValue(prop, new_value);
			}
			else {
				char buf[100];
				snprintf(buf, 100, "%s bad request to set property %s", _name.c_str(), property.c_str() );
				MessageLog::instance()->add(buf);
				DBG_MSG << buf << "\n";
			}
		}
		else {
			char buf[100];
			snprintf(buf, 150, "%s  setValue() could not find machine named %s for property %s",
					 _name.c_str(), name.c_str(), property.c_str() );
			MessageLog::instance()->add(buf);
			DBG_PROPERTIES << buf << "\n";
		}
	}
	else {
		Value property_val(property); // use this value in comparisons for performance

		if (property_val.token_id == ClockworkToken::POLLING_DELAY) {
			long new_delay = 0;
			if (new_value.asInteger(new_delay)) idle_time = new_delay;
			if (state_machine->token_id == ClockworkToken::SYSTEMSETTINGS) {
				*MachineInstance::polling_delay = new_delay;
			}
		}
		else if (property_val.token_id == ClockworkToken::TRACEABLE) {
			if (new_value == "TRUE") is_traceable = true;
			if (new_value == "FALSE") is_traceable = false;
		}

		// often variables are named in the GLOBAL list, if we find the property in that list
		// we try to change ask that machine to set its VALUE.
		if (state_machine->global_references.count(property)) {
			MachineInstance *global_machine = state_machine->global_references[property];
			if (global_machine && global_machine->state_machine->token_id != ClockworkToken::CONSTANT)
				global_machine->setValue("VALUE", new_value);
			return;
		}
		// try the current instance ofthe machine, then the machine class and finally the global symbols
		DBG_PROPERTIES << getName() << " setting property " << property << " to " << new_value << "\n";
		Value &prev_value = properties.lookup(property.c_str());

		if (prev_value == SymbolTable::Null && property_val.token_id != ClockworkToken::tokVALUE && property != _name) {
			// the 'property' may be a VARIABLE or CONSTANT machine declared locally or globally
			MachineInstance *global_machine = lookup(property);
			if (global_machine) {
				if ( global_machine->state_machine->token_id != ClockworkToken::CONSTANT ) {
					global_machine->setValue("VALUE", new_value);
					return;
				}
				NB_MSG << "attempt to set a value on constant " << property << ". ignored\n";
				return;
			}
		}

		if (state_machine->token_id == ClockworkToken::PUBLISHER && mq_interface && property_val.token_id == ClockworkToken::tokMessage )
		{
			std::string old_val(properties.lookup(property.c_str()).asString());
			mq_interface->publish(properties.lookup("topic").asString(), old_val, this);
		}

		if (new_value.kind == Value::t_integer && state_machine && state_machine->plugin && state_machine->plugin->filter) {
			new_value.iValue = state_machine->plugin->filter(this, new_value.iValue);
		}
		bool changed = (prev_value != new_value || (new_value != SymbolTable::Null && prev_value == SymbolTable::Null));
		if (changed ){
			setNeedsCheck();
			properties.add(property, new_value, SymbolTable::ST_REPLACE);
			if ( property_val.token_id == ClockworkToken::tokVALUE && io_interface) {
				char buf[100];
				errno = 0;
				long value =0; // TBD deal with sign
				if (new_value.asInteger(value))
				{
					//snprintf(buf, 100, "%s: updating output value to %ld\n", _name.c_str(),value);
					if (io_interface->address.is_signed)
						io_interface->setValue( (int32_t)(value & 0xffffffff));
					else
						io_interface->setValue( (uint32_t)(value & 0xffffffff));
					properties.add("VALUE", value, SymbolTable::ST_REPLACE);
				}
				else {
					snprintf(buf, 100, "%s: could not set value to %s", _name.c_str(), new_value.asString().c_str());
					MessageLog::instance()->add(buf);
					NB_MSG << buf << "\n";
				}
			}
			std::string property_name(modbusName(property, property_val));
			if (published) {
				Channel::sendPropertyChange(this, property.c_str(), new_value);

				// update modbus with the new value
				if (modbus_exports.count(property_name)){
					Channel::sendModbusUpdate(this, property_name, new_value);
				}
//				else
//					{ NB_MSG << property_name << " is not exported\n"; }
			}
//				else
//					{ NB_MSG << property_name << " is not published\n"; }
		}
		// only tell dependent machines to recheck predicates if the property
		// actually changes value
		if (changed) {
			DBG_M_PROPERTIES << "telling dependent machines about the change\n";
			setNeedsCheck();
			notifyDependents();
		}
	}
}

void MachineInstance::refreshModbus(cJSON *json_array) {

	std::map<int, std::string>::iterator iter = modbus_addresses.begin();
	while (iter != modbus_addresses.end()) {
		std::string full_name = (*iter).second;
		std::string short_name(full_name);
		if (short_name.rfind('.') != std::string::npos) {
			short_name = short_name.substr(short_name.rfind('.')+1);
		}
		int addr = (*iter).first & 0xffff;
		int group = (*iter).first >> 16;
		ModbusAddress info = modbus_exports[(*iter).second];
		if ((int)info.getGroup() != group) {
			NB_MSG << full_name << " index ("<<group<<"," <<addr<<")" << (*iter).first << " should be " << (*iter).second << "\n";
		}
		if ((int)info.getAddress() != addr) {
			NB_MSG << full_name << " index ("<<group<<"," <<addr<<")" << (*iter).first << " should be " << (*iter).second << "\n";
		}
		int length = info.length();
		Value value(0);
		switch(info.getSource()) {
			case ModbusAddress::machine:
				if (_type == "VARIABLE" || _type=="CONSTANT") {
					value = properties.lookup("VALUE");
				}
				else if (current_state.getName() == "on")
					value = 1;
				else 
					value = 0;
				break;
			case ModbusAddress::state:
				value =  (_name + "." + current_state.getName() == (*iter).second) ? 1 : 0;
				break;
			case ModbusAddress::command:
				value = 0;
				break;
			case ModbusAddress::property:
				if (properties.exists( short_name.c_str() ))
					value = properties.lookup( short_name.c_str() );
				else {
					MachineInstance *mi = lookup( (*iter).second );
					if (mi && (mi->_type == "VARIABLE" || mi->_type == "CONSTANT") ) {
						value = mi->getValue("VALUE");
					}
					else {
						std::stringstream ss; ss << "Error: " << full_name <<" has not been initialised";
						mi = lookup( (*iter).second );
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
		cJSON_AddItemToArray(item, cJSON_CreateNumber(group));
		cJSON_AddItemToArray(item, cJSON_CreateNumber(addr));
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
		cJSON_AddItemToArray(item, cJSON_CreateNumber(length));
		if (value.kind == Value::t_string || value.kind == Value::t_symbol)
			cJSON_AddItemToArray(item, cJSON_CreateString(value.sValue.c_str()));
		else
			cJSON_AddItemToArray(item, cJSON_CreateNumber(value.iValue));
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
		switch(ModbusAddress::toGroup(group)) {
			case ModbusAddress::discrete: data_type = "Discrete"; break;
			case ModbusAddress::coil: data_type = "Discrete"; break;
			case ModbusAddress::input_register: 
			case ModbusAddress::holding_register:  
						  if (info.length() == 1)
							  data_type = "Signed_int_16";
						  else if (info.length() == 2)
							  data_type = "Signed_int_32";
						  else
							  data_type = "Ascii_String";
						  break;
			default: data_type = "Unknown";
		}
		if (owner)
			out << group << ":" << std::setfill('0') << std::setw(5) << addr
				<< "\t" << owner->getName() << "." << ((group==0) ? "cmd_": "") << (*iter).second
				<< "\t" << data_type
				<< "\t" << info.length()
				<< "\n";
		else {
			std::string name((*iter).second);
			size_t found = name.rfind(".");
			if (group == 0 && found != std::string::npos) {
				name.replace(found, 1, ".cmd_");
			}
			out << group << ":" << std::setfill('0') << std::setw(5) << addr
				<< "\t" << name
				<< "\t" << data_type
				<< "\t" << info.length()
				<< "\n";
		}
		iter++;
	}

}

// Modbus


ModbusAddress MachineInstance::addModbusExport(std::string name, ModbusAddress::Group g, unsigned int n, 
		ModbusAddressable *owner, ModbusAddress::Source src, const std::string &full_name) {
	if (ModbusAddress::preset_modbus_mapping.count(full_name) != 0) {
		ModbusAddressDetails info = ModbusAddress::preset_modbus_mapping[full_name];
		DBG_MODBUS << _name << " " << name << " has predefined address: " << info.group<<":"<<std::setfill('0')<<std::setw(5)<<info.address << "\n";
		ModbusAddress addr(ModbusAddress::toGroup(info.group), info.address, info.len, owner, src, full_name);
		modbus_exports[name] = addr;
		return addr;
	}
	else {
		ModbusAddress addr = ModbusAddress::alloc(g, n, this, src, full_name);
		DBG_MODBUS << _name << " " << name << " allocated new address " << addr << "\n";
		modbus_exports[name] = addr;
		return addr;
	}
}

void MachineInstance::setupModbusPropertyExports(std::string property_name, ModbusAddress::Group grp, int size) {
	std::string full_name;
	if (owner) full_name = owner->getName() + ".";
	std::string name;
	if (_name != property_name) name = _name + ".";
	name += property_name;
	switch(grp) {
		case ModbusAddress::discrete:
			addModbusExport(name, ModbusAddress::discrete, size, this, ModbusAddress::property, full_name+name); 
			break;
		case ModbusAddress::coil:
			addModbusExport(name, ModbusAddress::coil, size, this, ModbusAddress::property, full_name+name); 
			break;
		case ModbusAddress::input_register:
			addModbusExport(name, ModbusAddress::input_register, size, this, ModbusAddress::property, full_name+name); 
			break;
		case ModbusAddress::holding_register:
			addModbusExport(name, ModbusAddress::holding_register, size, this, ModbusAddress::property, full_name+name);
			break;
		case ModbusAddress::string:
			addModbusExport(name, ModbusAddress::input_register, size, this, ModbusAddress::property, full_name+name);
			break;
		default: ;
	}
}

void MachineInstance::setupModbusInterface() {

	if (modbus_addresses.size() != 0) return; // already done
	DBG_MODBUS << fullName() << " setting up modbus\n";
	std::string full_name = fullName();

	bool self_discrete = false;
	bool self_coil = false;
	bool self_reg = false;
	bool self_rwreg = false;
	long str_length = 0;

	// workout the export type for this machine
	ExportType export_type = none;

	bool exported = properties.exists("export");
	if (exported) {
		Value export_type_val = properties.lookup("export");
		if (export_type_val != "false") {
			if (_type == "POINT") {
				Value &type = properties.lookup("type");
				if (type != SymbolTable::Null) {
					if (type == "Input") {
						self_discrete = true; 
						export_type=discrete;
					}
					else if (type == "Output") {
						if (export_type_val=="rw") {
							self_coil = true;
							export_type=coil;
						}
						else {
							self_discrete = true;
							export_type=coil;
						}
					}
					else if (type=="AnalogueInput") {
						self_reg = true;
						export_type=reg;
					}
					else if (type=="AnalogueOutput") {
						// default to readonly export
						if (export_type_val=="rw") {
							self_rwreg = true;
							export_type=rw_reg;
						}
						else {
							self_reg = true;
							export_type=reg;
						}
					}
				}
			}
			else
				if (export_type_val=="rw") {
					self_coil = true;
					export_type=coil;
				}
				else if (export_type_val=="reg") {
					self_reg = true;
					export_type=reg;
				}
				else if (export_type_val=="rw_reg") {
					self_rwreg = true;
					export_type=rw_reg;
				}
				else if (export_type_val=="reg32") {
					self_reg = true;
					export_type=reg32;
				}
				else if (export_type_val=="rw_reg32") {
					self_rwreg = true;
					export_type=rw_reg32;
				}
				else if (export_type_val=="str") {
					Value &export_size = properties.lookup("strlen");
					self_reg = true; 
					export_type=str;
					export_size.asInteger(str_length);
				}
				else if (export_type_val=="rw_str") {
					Value &export_size = properties.lookup("strlen");
					self_rwreg = true;
					export_type=str;
					export_size.asInteger(str_length);
				}
				else {
					self_discrete = true;
					export_type=discrete;
				}
		}
	}
	if (_type != "VARIABLE" && _type != "CONSTANT") {// export property refers to VALUE export type, not the machine
		modbus_exported = export_type;		
	}

	// register the individual properties 

	if (_type != "VARIABLE" && _type != "CONSTANT") {
		// exporting 'self' (either 'on' state or 'VALUE' property depending on export type)
		if (self_coil) {
			modbus_address = addModbusExport(_name, ModbusAddress::coil, 1, this, ModbusAddress::machine, full_name);
		}
		else if (self_discrete) {
			modbus_address = addModbusExport(_name, ModbusAddress::discrete, 1, this, ModbusAddress::machine, full_name);
			//addModbusExportname(_name].setName(full_name);
		}
		else if (self_reg) {
			int len = 1;
			if (export_type == reg) {
				len = 1;
			}
			else if (export_type == reg32) {
				len = 2;
			}
			else if (export_type == str && str_length == 0) {
				len=80;
			}
			modbus_address = addModbusExport(_name, ModbusAddress::input_register, len, this, ModbusAddress::machine, full_name);
			//modbus_exports[_name].setName(full_name);
		}
		else if (self_rwreg) {
			int len = 1;
			if (export_type == rw_reg32) {
				len = 2;
			}
			else if (export_type == str && str_length == 0) {
				len=80;
			}
			addModbusExport(_name, ModbusAddress::holding_register, len, this, ModbusAddress::machine, full_name);
			//modbus_exports[name(_name].setName(full_name);
		}
	}

	// exported states
	BOOST_FOREACH(std::string s, state_machine->state_exports) {
		std::string name = _name + "." + s;
		addModbusExport(name, ModbusAddress::discrete, 1, this, ModbusAddress::state, full_name+"."+s);
		//modbus_exports[name(name].setName(full_name+"."+s);
	}

	// exported commands
	BOOST_FOREACH(std::string s, state_machine->command_exports) {
		std::string name = _name + "." + s;
		addModbusExport(name, ModbusAddress::coil, 1, this, ModbusAddress::command, full_name+"."+s);
		//addModbusExportname(name].setName(full_name+"."+s);
	}

	if (_type == "VARIABLE" || _type == "CONSTANT") {
		std::string property_name(_name);
		switch(export_type) {
			case none: break;
			case discrete:
				   setupModbusPropertyExports(property_name, ModbusAddress::discrete, 1);
				   break;
			case coil:
				   setupModbusPropertyExports(property_name, ModbusAddress::coil, 1);
				   break;
			case reg:
				   setupModbusPropertyExports(property_name, ModbusAddress::input_register, 1);
				   break;
			case rw_reg:
				   setupModbusPropertyExports(property_name, ModbusAddress::holding_register, 1);
				   break;
			case reg32:
				   setupModbusPropertyExports(property_name, ModbusAddress::input_register, 2);
				   break;
			case rw_reg32:
				   setupModbusPropertyExports(property_name, ModbusAddress::holding_register, 2);
				   break;
			case str:
				   if (self_reg)
					   setupModbusPropertyExports(property_name, ModbusAddress::input_register, (int)str_length);
				   else
					   setupModbusPropertyExports(property_name, ModbusAddress::holding_register, (int)str_length);
				   break;
		}
	}

	BOOST_FOREACH(ModbusAddressTemplate mat, state_machine->exports) {
		//if (export_type == none) continue;
		setupModbusPropertyExports(mat.property_name, mat.kind, mat.size);
	}

	assert(modbus_addresses.size() == 0);
	std::map<std::string, ModbusAddress >::iterator iter = modbus_exports.begin();
	while (iter != modbus_exports.end()) {
		const ModbusAddress &ma = (*iter).second;
		int index = ((int)ma.getGroup() << 16) + ma.getAddress();
		if ( modbus_addresses.count(index) != 0) {
			NB_MSG << _name << " " << (*iter).first << " address " << ma << " already recorded as " << modbus_addresses[index] << "\n";
		}
		assert(modbus_addresses.count(index) == 0);
		modbus_addresses[index] = (*iter).first;
		iter++;
	}
	if (modbus_addresses.size() != modbus_exports.size()){
		NB_MSG << _name << " modbus export inconsistent. addresses: " << modbus_addresses.size() << " indexes " << modbus_exports.size() << "\n";
	}
	assert(modbus_addresses.size() == modbus_exports.size());

}

void MachineInstance::modbusUpdated(ModbusAddress &base_addr, unsigned int offset, const char *new_value) {
	std::string name = fullName();
	DBG_MODBUS << name << " modbusUpdated " << base_addr << " " << offset << " " << new_value << "\n";
	int index = (base_addr.getGroup() <<16) + base_addr.getAddress() + offset;
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
	DBG_MODBUS << name << " local ModbusAddress found: " << addr<< "\n";

	if (addr.getGroup() == ModbusAddress::holding_register) {
		DBG_MODBUS << name << " holding register update\n";
		std::string property_name = modbus_addresses[index];
		DBG_MODBUS << _name << " set property " << property_name << " via modbus index " << index << " (" << addr << ")\n";
		if (property_name == _name)
			setValue("VALUE", new_value);
		else
			setValue(property_name, new_value);
	}
	else {
		NB_MSG << name << " unexpected modbus group for write operation " << addr << "\n";
	}

}

void MachineInstance::modbusUpdated(ModbusAddress &base_addr, unsigned int offset, int new_value) {
	std::string name(fullName());
	DBG_MODBUS << name << " modbusUpdated " << base_addr << " " << offset << " " << new_value << "\n";
	int index = (base_addr.getGroup() <<16) + base_addr.getAddress() + offset;
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
	DBG_MODBUS << name << " local ModbusAddress found: " << addr<< "\n";	

	if (addr.getGroup() == ModbusAddress::coil || addr.getGroup() == ModbusAddress::discrete){

		if (addr.getSource() == ModbusAddress::machine) {
			// crosscheck - the machine must have been exported read/write
			if (!properties.exists("export")) {
				char buf[100];
				snprintf(buf, 100, "received a modbus update for machine %s but that machine has no export property", name.c_str());
				MessageLog::instance()->add(buf);
			}

			DBG_MODBUS << "setting state of " << name << " due to modbus command\n";
			if (new_value) {
				SetStateActionTemplate ssat(CStringHolder("SELF"), "on" );
				enqueueAction(ssat.factory(this)); // execute this state change once all other actions are complete
			}
			else {
				SetStateActionTemplate ssat(CStringHolder("SELF"), "off" );
				enqueueAction(ssat.factory(this)); // execute this state change once all other actions are complete
			}
			return;
		}
		else if (addr.getSource() == ModbusAddress::command) {
			if (new_value) {
				std::string &cmd_name = modbus_addresses[index];
				DBG_MODBUS << _name << " executing command " << cmd_name << "\n";
				// execute this command once all other actions are complete
				handle(Message(cmd_name.c_str()), this, true); // fire the trigger when the command is done
			}
			return;
		}
		else if (addr.getSource() == ModbusAddress::property) {
			std::string property_name = modbus_addresses[index];
			DBG_MODBUS << _name << " set property " << property_name << " via modbus index " << index << " (" << addr << ")\n";
			if (name == _name)
				setValue("VALUE", new_value);
			else
				setValue(property_name, new_value);
		}
		else
			DBG_MODBUS << _name << " received update for out-of-range coil" << offset << "\n";

	}
	else if (addr.getGroup() == ModbusAddress::holding_register) {
		DBG_MODBUS << name << " holding register update\n";
		std::string property_name = modbus_addresses[index];
		DBG_MODBUS << _name << " set property " << property_name << " via modbus index " << index << " (" << addr << ")\n";
		if (property_name == _name)
			setValue("VALUE", new_value);
		else
			setValue(property_name, new_value);
	}
	else {
		NB_MSG << name << " unexpected modbus group for write operation " << addr << "\n";
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
	DBG_M_MODBUS << _name << " found device " << devname << " matching address " << addr.getGroup() << ":" << addr.getAddress()  + offset << "\n";
	if (devname == _name && addr.getAddress() == 0) {
		if (_type != "VARIABLE" && _type != "CONSTANT") {
			if (current_state.getName() == "on")
				return 1;
			else
				return 0;
		}
		else {
			const Value &v = getValue("VALUE");
			long intVal;
			if (v.asInteger(intVal))
				return (int)intVal;
			else
				return 0;
		}
	}
	return 0;
}



// Error states (not used yet)
bool MachineInstance::inError() { 
	return error_state != 0; 
}

void MachineInstance::setError(int val) { 
	if (error_state) return;
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
	if (state_machine) setState(state_machine->initial_state); 
}

void MachineInstance::ignoreError() {
	error_state = 0;
}

MachineDetails::MachineDetails(const char *nam, const char *cls, std::list<Parameter> &params,
			   const char *sf, int sl, SymbolTable &props, MachineInstance::InstanceType kind)
: machine_name(nam), machine_class(cls), source_file(sf), source_line(sl),
instance_type(kind)
{
	std::copy(parameters.begin(), parameters.end(),  back_inserter(parameters));

	SymbolTableConstIterator iter = props.begin();
	while (iter != props.end()) {
		const std::pair<std::string, Value> &p = *iter;
		properties.push_back(p);
		iter++;
	}
}

MachineInstance *MachineDetails::instantiate() {
	MachineInstance *machine = MachineInstanceFactory::create(machine_name.c_str(),
		machine_class.c_str(), instance_type);
	machine->setDefinitionLocation(source_file.c_str(), source_line);
	std::copy(parameters.begin(), parameters.end(),  back_inserter(machine->parameters));
	machine->setProperties(properties);
	return machine;
}

