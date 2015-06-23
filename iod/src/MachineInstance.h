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

#ifndef cwlang_MachineInstance_h
#define cwlang_MachineInstance_h

#include <list>
#include <map>
#include <set>
#include <vector>
#include <sstream>
#include <sys/time.h>
#include <cassert>
#include "symboltable.h"
#include "Message.h"
#include "State.h"
#include "Action.h"
#include <boost/foreach.hpp>
#include "Plugin.h"
#include "Expression.h"
#include "ModbusInterface.h"
#include "SetStateAction.h"
#include "MachineCommandAction.h"
#include "Statistic.h"

extern SymbolTable globals;

uint64_t nowMicrosecs();
uint64_t nowMicrosecs(const struct timeval &now);
int64_t get_diff_in_microsecs(const struct timeval *now, const struct timeval *then);
int64_t get_diff_in_microsecs(uint64_t now, const struct timeval *then);
int64_t get_diff_in_microsecs(const struct timeval *now, uint64_t then);

class MachineInstance;
class MachineClass;

struct MoveStateAction;

extern std::map<std::string, MachineInstance*>machines;
extern std::map<std::string, MachineClass*> machine_classes;

class Transition {
public:
    State source;
    State dest;
    Message trigger;
    Condition *condition;
    Transition(State s, State d, Message t, Predicate *p=0);
    Transition(const Transition &other);
    Transition &operator=(const Transition &other);
    ~Transition();
};

class ConditionHandler {
public:
	ConditionHandler(const ConditionHandler &other);
	ConditionHandler &operator=(const ConditionHandler &other);
	ConditionHandler() : timer_val(0), timer_op(opGE), action(0), trigger(0), uses_timer(false), triggered(false) {}
    
    bool check(MachineInstance *machine);
    void reset();
    
	Condition condition;
	std::string command_name;
	std::string flag_name;
	Value timer_val;
    PredicateOperator timer_op;
	Action *action;
	Trigger *trigger;
	bool uses_timer;
	bool triggered;
};

class StableState : public TriggerOwner {
public:

	StableState() : state_name(""),
 		uses_timer(false), timer_val(0), trigger(0), subcondition_handlers(0),
		owner(0), name("") { }

	StableState(const char *s, Predicate *p) : state_name(s),
		condition(p), uses_timer(false), timer_val(0),
			trigger(0), subcondition_handlers(0), owner(0), name(s)
 	{
		uses_timer = p->usesTimer(timer_val); 
	}

	StableState(const char *s, Predicate *p, Predicate *q) 
		: state_name(s),
			condition(p), uses_timer(false), timer_val(0), trigger(0),
			subcondition_handlers(0), owner(0), name(s) {
		uses_timer = p->usesTimer(timer_val); 
	}
    
    ~StableState();

    bool operator<(const StableState &other) const {  // used for std::sort
        if (!condition.predicate || !other.condition.predicate) return false;
        return condition.predicate->priority < other.condition.predicate->priority;
    };
    std::ostream &operator<< (std::ostream &out)const { return out << name; }
	StableState& operator=(const StableState* other);
    StableState (const StableState &other);
        
    void setOwner(MachineInstance *m) { owner = m; }
    void fired(Trigger *trigger);
    void triggerFired(Trigger *trigger);
    void refreshTimer();
	
	std::string state_name;
	Condition condition;
	bool uses_timer;
	Value timer_val;
	Trigger *trigger;
	std::list<ConditionHandler> *subcondition_handlers;
    MachineInstance *owner;
	Value name;
};
std::ostream &operator<<(std::ostream &out, const StableState &ss);

class Parameter {
public:
    Value val;
    SymbolTable properties;
    MachineInstance *machine;
    std::string real_name;
    Parameter(Value v);
    Parameter(const char *name, const SymbolTable &st);
    std::ostream &operator<< (std::ostream &out)const;
    Parameter(const Parameter &orig);
};
std::ostream &operator<<(std::ostream &out, const Parameter &p);

// modbus interfacing
class ModbusAddressTemplate {
public:
    //Group - discrete, coil, etc
    //Size - number of units of the address type (always 1 for discretes and coils)
	std::string property_name;
	ModbusAddress::Group kind;
	int size; // number of units
	ModbusAddressTemplate(const std::string &property, ModbusAddress::Group g, int n_units) 
		: property_name(property), kind(g), size(n_units) {	}
};

class MachineClass {
public:
    MachineClass(const char *class_name);
    SymbolTable properties;
    std::vector<Parameter> parameters;
    std::vector<Parameter> locals;
    std::list<State> states;
    std::set<std::string> state_names;
    std::vector<StableState> stable_states;
    std::map<std::string, MachineCommandTemplate*> commands;
    std::map<Message, MachineCommandTemplate*> enter_functions;
    std::map<Message, MachineCommandTemplate*> receives;
	std::map<std::string, MachineInstance*>global_references;
	std::map<std::string, Value> options;
    std::list<Transition> transitions; 
	std::list<ModbusAddressTemplate> exports;
	std::vector<std::string> state_exports;
	std::vector<std::string> command_exports;
    static std::map<std::string, MachineClass> machine_classes;
    void defaultState(State state);
	bool isStableState(State &state);
    State *findState(const char *name);
    virtual ~MachineClass() { all_machine_classes.remove(this); }
	void disableAutomaticStateChanges();
	void enableAutomaticStateChanges();
    static MachineClass *find(const char *name);

	virtual void addProperty(const char *name); // used in interfaces to list synced properties
	virtual void addCommand(const char *name); // used in interfaces to list permitted commands

    State default_state;
	State initial_state;
    std::string name;
    static std::list<MachineClass*> all_machine_classes;
	bool allow_auto_states;
    int token_id;
    Plugin *plugin;
    long polling_delay;

	std::set<std::string> property_names;
	std::set<std::string> command_names;
private:
    MachineClass();
    MachineClass(const MachineClass &other);
};

class MachineInterface : public MachineClass {
public:
    MachineInterface(const char *class_name);
    virtual ~MachineInterface();
    static std::map<std::string, MachineInterface *> all_interfaces;
//	void addProperty(const char *name);
//	void addCommand(const char *name);
};

/*
 
 // flag is an internal machine class that implements a boolean switch
 MachineClass flag;
 flag.states.push_back("on");
 flag.states.push_back("off");
 flag.receives["turnOn"] = Action("set_state", "on");
 flag.receives["turnOff"] = Action("set_state", "off");
 
 // Action(
 
 */

struct HardwareAddress {
    int io_offset;
    int io_bitpos;
	HardwareAddress(int offs, int bitp) : io_offset(offs), io_bitpos(bitp) { }
	HardwareAddress() : io_offset(0), io_bitpos(0) {}
};

class IOComponent;
class MQTTModule;
struct cJSON;

class MachineEvent {
public:
	MachineInstance *mi;
	Message *msg;
	MachineEvent(MachineInstance *, Message *);
	MachineEvent(MachineInstance *, const Message &);
	~MachineEvent();
private:
	MachineEvent(const MachineEvent&);
	MachineEvent &operator=(const MachineEvent&);
};


class MachineInstance : public Receiver, public ModbusAddressable, public TriggerOwner {
    friend class MachineInstanceFactory;
public:
    enum PollType { BUILTINS, NO_BUILTINS};
	enum InstanceType { MACHINE_INSTANCE, MACHINE_TEMPLATE, MACHINE_SHADOW };
protected:
    MachineInstance(InstanceType instance_type = MACHINE_INSTANCE);
    MachineInstance(CStringHolder name, const char * type, InstanceType instance_type = MACHINE_INSTANCE);
    
public:
	virtual ~MachineInstance();
	virtual Receiver *asReceiver() { return this; }
	class SharedCache;
	class Cache;
    
    void triggerFired(Trigger *trig);

    void addParameter(Value param, MachineInstance *machine = 0);
    void removeParameter(int which);
    void addLocal(Value param, MachineInstance *machine = 0);
    void removeLocal(int index);
    void setProperties(const SymbolTable &props);
    
    // record where in the program this machine was defined
    void setDefinitionLocation(const char *file, int line_no);
    
    // tbd record which parts of the program refer to this machine
    //void addReferenceLocation(const char *file, int line_no);
    
    std::ostream &operator<<(std::ostream &out)const;
    void describe(std::ostream& out);

	static void add_io_entry(const char *name, unsigned int io_offset, unsigned int bit_offset);

    virtual bool receives(const Message&, Transmitter *t);
	Action::Status execute(const Message&m, Transmitter *from);
    virtual void handle(const Message&, Transmitter *from, bool send_receipt = false);
	virtual void sendMessageToReceiver(Message *m, Receiver *r = NULL, bool expect_reply = false);

    virtual void idle();
		//virtual bool hasWork() { return has_work; }
	void collect(const Package &package);

	std::map<std::string, MachineInstance *> localised_names;
	MachineInstance *lookup_cache_miss(const std::string &seek_machine_name);
    MachineInstance *lookup(Parameter &param);
    MachineInstance *lookup(Value &val);
    MachineInstance *lookup(const char *);
    MachineInstance *lookup(const std::string &name);
    Value &getValue(std::string property); // provides the current value of an object accessible in the scope of this machine
    Value *getValuePtr(Value &property); // provides the current value of an object accessible in the scope of this machine
    Value &getValue(Value &property); // provides the current value of an object accessible in the scope of this machine
    virtual void setValue(const std::string &property, Value new_value);
    Value *resolve(std::string property); // provides a pointer to the value of an object that can be evaluated in the future
    
    void setStateMachine(MachineClass *machine_class);
    bool stateExists(State &s);
    bool hasState(const std::string &state_name) const;
	Value *lookupState(const std::string &state_name);
	Value *lookupState(const Value &);
    void listenTo(MachineInstance *m);
    void stopListening(MachineInstance *m);
    bool setStableState(); // returns true if a state change was made
	virtual bool isShadow(); // is this machine a shadow instance?
    
    std::string &fullName() const;
    
    std::string _type;
    std::vector<Parameter> parameters;    
    std::vector<Parameter> locals;    
    std::vector<StableState> stable_states;
    std::map<std::string, MachineCommand*> commands;
    std::map<Message, MachineCommand*> enter_functions;
    std::map<Message, MachineCommand*> receives_functions;
    std::set<Transmitter *> listens;
    std::list<Transition> transitions;

    Action *executingCommand(); // returns the action currently executing
	std::list<Action*>active_actions;
	void displayActive(std::ostream &out);
	void start(Action *a);
	void stop(Action *a);
	void push(Action *new_action);
	Action *findHandler(Message&msg, Transmitter *t, bool response_required = false);
    void enqueueAction(Action *a);

	State &getCurrent() { return current_state; }
	const char *getCurrentStateString() { return current_state.getName().c_str(); }
	
	void addDependancy(MachineInstance *m);
	void removeDependancy(MachineInstance *m);
	
	// this depends on machine m if it is in m's list of dependants or if the machine
	// is in this machines listen list.
	bool dependsOn(Transmitter *m);

	// indicate that dependent machine should check their state
    void notifyDependents(); 

	// forward the message to dependents and notify them to check their state
    void notifyDependents(Message &msg); 
	
	bool needsCheck();
	void resetNeedsCheck();
	void resetTemporaryStringStream();

    static bool processAll(uint32_t max_time, PollType which);
	//static void updateAllTimers(PollType which);
	//void updateTimer(long dt);
	static bool checkStableStates(uint32_t max_time);
	static void checkPluginStates();
	static size_t countAutomaticMachines() { return automatic_machines.size(); }
	static void displayAutomaticMachines();
	static void displayAll();
    
	static MachineInstance *find(const char *name);
	static std::list<MachineInstance*>::iterator begin() { return all_machines.begin(); }
	static std::list<MachineInstance*>::iterator end()  { return all_machines.end(); }
    
    static std::list<MachineInstance*>::iterator begin_active() { return active_machines.begin(); }
    static std::list<MachineInstance*>::iterator end_active() { return active_machines.end(); }
    static void addActiveMachine(MachineInstance* m) { active_machines.push_back(m); }
    void markActive();
    void markPassive();
		void markPlugin();
    
	MachineClass *getStateMachine() const { return state_machine; }
	void setInitialState();
	Trigger *setupTrigger(const std::string &machine_name, const std::string &message, const char *suffix);
	const Value *getTimerVal();
	Value *getCurrentStateVal() { return &current_state_val; }
    Value *getCurrentValue() { return &current_value_holder; }

	bool uses(MachineInstance *other);
	std::set<MachineInstance*>depends;
	
	virtual void enable();
	virtual void resume();
	void resume(State &state);
    void resumeAll(); // resume all disable sub-machines
	void disable();
	inline bool enabled() const { return is_enabled; }
	void clearAllActions();
	void checkActions();

	// basic lock functionality 
	bool lock(MachineInstance *requester) { if (locked && locked != requester) return false; else {locked = requester; return true; } }
	bool unlock(MachineInstance *requester) { if (locked != requester) return false; else { locked = 0; return true; } }
    MachineInstance *locker() const { return locked; }
	
	// basic modbus interface
	ModbusAddress addModbusExport(std::string name, ModbusAddress::Group g, unsigned int n, ModbusAddressable *owner, ModbusAddress::Source src, const std::string &full_name);
	std::map<std::string, ModbusAddress >modbus_exports;
	std::map<int, std::string>modbus_addresses;
	void setupModbusInterface();
	void setupModbusPropertyExports(std::string property_name, ModbusAddress::Group grp, int size);
	void refreshModbus(cJSON *json_array); // update all exported values
	int getModbusValue(ModbusAddress &addr, unsigned int offset, int len);
	void modbusUpdated(ModbusAddress &addr, unsigned int offset, int new_value);
	void modbusUpdated(ModbusAddress &addr, unsigned int offset, const char *new_value);
	void exportModbusMapping(std::ostream &out);
	bool isModbusExported() { return modbus_exported != none; }
    
    bool isTraceable() { return is_traceable.bValue; }
	
	// error states are outside of the normal processing for a state machine;
	// they cause other processing to halt and trigger receipt of a message: ERROR
	bool inError();
	void setError(int val);
	void resetError();
	void ignoreError();

	// debug messaging
	void setDebug(bool which);
	bool isActive() { return is_active; }

	IOComponent *io_interface;
	MQTTModule *mq_interface;
	MachineInstance *owner;

	SymbolTable properties;
	std::string definition_file;
	int definition_line;
	struct timeval start_time; // time the current state started
	struct timeval disabled_time; // time the current state started

	static void sort();
    
    virtual void setNeedsCheck();
    uint64_t lastStateEvaluationTime() { return last_state_evaluation_time; }
    void updateLastEvaluationTime();
    
    virtual long filter(long val) { return val; }
    
    void publish();
    void unpublish();
    
    static void forceStableStateCheck();
    static void forceIdleCheck();
    static bool workToDo();
	static std::set<MachineInstance*>& busyMachines();
	static std::list<Package*>& pendingEvents();
    static std::set<MachineInstance*>& pluginMachines();

protected:
	int needs_check;
public:
	bool uses_timer;
	
protected:
	InstanceType my_instance_type;
    MoveStateAction *state_change; // this is set during change between stable states
    MachineClass *state_machine;
    State current_state;
	virtual Action::Status setState(State &new_state, bool resume = false);
    virtual Action::Status setState(const char *new_state, bool resume = false);
	bool is_enabled;
	Value state_timer;
	MachineInstance *locked;
	ModbusAddress modbus_address;
	std::vector<std::string>state_names; // used for mapping modbus offsets to states
	std::vector<std::string>command_names; // used for mapping modbus offsets to states
	ModbusAddressable::ExportType modbus_exported;
	
	int error_state; // error number of the current error if any
	State saved_state; // save state before error
	Value current_state_val;
    bool is_active; // is this machine active or passive?
    Value current_value_holder;
	std::stringstream ss; // saves recreating string stream for temporary use
    uint64_t last_state_evaluation_time; // dynamic value check against this before recalculating
public:
	Statistic stable_states_stats;
	Statistic message_handling_stats;
	void * data; // plugin data
	uint64_t idle_time; // amount of time to be idle between state polls (microsec)
	uint64_t next_poll;

	static Value *polling_delay;
	Value is_traceable;
	int published;
	static SharedCache *shared;
	Cache *cache;
	unsigned int action_errors;
private:
	static std::map<std::string, HardwareAddress> hw_names;
    MachineInstance &operator=(const MachineInstance &orig);
    MachineInstance(const MachineInstance &other);
protected:
    static std::list<MachineInstance*> all_machines;
    static std::list<MachineInstance*> automatic_machines; // machines with auto state changes enabled
    static std::list<MachineInstance*> active_machines; // machines that require idle() processing
    static std::list<MachineInstance*> shadow_machines; // machines that shadow remote machines
    static std::set<MachineInstance*> busy_machines; // machines that have work queued to them
    static std::set<MachineInstance*> pending_state_change; // machines that need to check their stable states
    static std::set<MachineInstance*> plugin_machines; // machines that have plugins
    static std::list<Package*> pending_events; // machines that shadow remote machines
    static unsigned int num_machines_with_work;
    static unsigned int total_machines_needing_check;

	friend struct SetStateAction;
	friend struct MoveStateAction;
	friend struct SetIOStateAction;
	friend struct ExpressionAction;
	friend struct IODCommandToggle;
    friend struct IODCommandSetStatus;
    friend class ConditionHandler;
    friend class SetListEntriesAction;
    friend class PopListBackValue;
    friend class PopListFrontValue;
    friend class ItemAtPosValue;
    friend void fixListState(MachineInstance &list);
	friend void initialiseOutputs();

	friend int changeState(void *s, const char *new_state);
};

std::ostream &operator<<(std::ostream &out, const MachineInstance &m);

class MachineShadowInstance : public MachineInstance {
protected:
    MachineShadowInstance(InstanceType instance_type = MACHINE_INSTANCE);
    MachineShadowInstance(CStringHolder name, const char * type, InstanceType instance_type = MACHINE_INSTANCE);

private:
    MachineShadowInstance &operator=(const MachineShadowInstance &orig);
    MachineShadowInstance(const MachineShadowInstance &other);
    MachineShadowInstance *settings;

public:
    MachineShadowInstance();
    ~MachineShadowInstance();
    virtual void idle();
	virtual bool isShadow() { return true; }

	virtual Action::Status setState(State &new_state, bool resume = false);
	virtual Action::Status setState(const char *new_state, bool resume = false);


    friend class MachineInstanceFactory;
};

class CounterRateFilterSettings;
class CounterRateInstance : public MachineInstance {
protected:
    CounterRateInstance(InstanceType instance_type = MACHINE_INSTANCE);
    CounterRateInstance(CStringHolder name, const char * type, InstanceType instance_type = MACHINE_INSTANCE);
public:
    ~CounterRateInstance();
    void setValue(const std::string &property, Value new_value);
    long filter(long val);
    virtual void idle();
	//virtual bool hasWork();
    CounterRateFilterSettings *getSettings() { return settings; }
private:
    CounterRateInstance &operator=(const CounterRateInstance &orig);
    CounterRateInstance(const CounterRateInstance &other);
    CounterRateFilterSettings *settings;
    
    friend class MachineInstanceFactory;
};

class RateEstimatorInstance : public MachineInstance {
protected:
    RateEstimatorInstance(InstanceType instance_type = MACHINE_INSTANCE);
    RateEstimatorInstance(CStringHolder name, const char * type, InstanceType instance_type = MACHINE_INSTANCE);
public:
    ~RateEstimatorInstance();
    void setValue(const std::string &property, Value new_value);
    long filter(long val);
    virtual void setNeedsCheck();
    virtual void idle();
	//virtual bool hasWork();
    CounterRateFilterSettings *getSettings() { return settings; }
private:
    RateEstimatorInstance &operator=(const RateEstimatorInstance &orig);
    RateEstimatorInstance(const RateEstimatorInstance &other);
    CounterRateFilterSettings *settings;
    
    friend class MachineInstanceFactory;
};

#include "dynamic_value.h"
class MachineValue : public DynamicValue {
public:
    MachineValue(MachineInstance *mi, std::string name): machine_instance(mi), local_name(name) { }
    void setMachineInstance(MachineInstance *mi) { machine_instance = mi; }
    Value &operator()(MachineInstance *m)  {
        if (!machine_instance) { machine_instance = m->lookup(local_name); }
        if (!machine_instance) { last_result = SymbolTable::Null; return last_result; }
        if (machine_instance->_type == "VARIABLE" || machine_instance->_type == "CONSTANT") {
            last_result = machine_instance->getValue("VALUE");
            return last_result;
        }
        else {
            last_result = *machine_instance->getCurrentStateVal();
            return last_result;
        }
    }
    std::ostream &operator<<(std::ostream &out ) const;
    DynamicValue *clone() const;
    virtual void flushCache();
protected:
    MachineInstance *machine_instance;
    std::string local_name;
};

class MachineInstanceFactory {
public:
    static MachineInstance *create(MachineInstance::InstanceType instance_type = MachineInstance::MACHINE_INSTANCE);
    static MachineInstance *create(CStringHolder name, const char * type, MachineInstance::InstanceType instance_type = MachineInstance::MACHINE_INSTANCE);
};

// during the parsing process we build a list of
// things to instantiate but don't actually do it until
// all files have been loaded.

class MachineDetails {
	std::string machine_name;
	std::string machine_class;
	std::list<Parameter> parameters;
	std::string source_file;
	int source_line;
	SymbolTable properties;
	MachineInstance::InstanceType instance_type;

	MachineDetails(const char *nam, const char *cls, std::list<Parameter> &params,
				   const char *sf, int sl, SymbolTable &props, MachineInstance::InstanceType kind);
	MachineInstance *instantiate();
};


#endif
