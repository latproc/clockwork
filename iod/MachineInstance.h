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

#ifndef latprocc_MachineInstance_h
#define latprocc_MachineInstance_h

#include <list>
#include <map>
#include <vector>
#include <sstream>
#include <sys/time.h>
#include <cassert>
#include "symboltable.h"
#include "Message.h"
#include "State.h"
#include "Action.h"
#include <boost/foreach.hpp>
#include "Expression.h"
#include "ModbusInterface.h"
#include "SetStateAction.h"
#include "MachineCommandAction.h"

extern SymbolTable globals;

long get_diff_in_microsecs(struct timeval *now, struct timeval *then);

class MachineInstance;
class MachineClass;

class MoveStateAction;

extern std::map<std::string, MachineInstance*>machines;
extern std::map<std::string, MachineClass*> machine_classes;

struct Transition {
    State source;
    State dest;
    Message trigger;
    Condition *condition;
    Transition(State s, State d, Message t, Predicate *p=0);
    Transition(const Transition &other);
    Transition &operator=(const Transition &other);
};

struct ConditionHandler {
	ConditionHandler(const ConditionHandler &other);
	ConditionHandler &operator=(const ConditionHandler &other);
	ConditionHandler() : action(0), trigger(0), uses_timer(false), triggered(false) {}
	Condition condition;
	std::string command_name;
	std::string flag_name;
	Value timer_val;
	Action *action;
	Trigger *trigger;
	bool uses_timer;
	bool triggered;
};

struct StableState : public TriggerOwner {
    std::string state_name;
    Condition condition;

	StableState() : state_name(""), uses_timer(false), timer_val(0), trigger(0), subcondition_handlers(0), owner(0) { }

	StableState(const char *s, Predicate *p) : state_name(s), condition(p), uses_timer(false), timer_val(0), 
			trigger(0), subcondition_handlers(0), owner(0) { 
		uses_timer = p->usesTimer(timer_val); 
	}

	StableState(const char *s, Predicate *p, Predicate *q) 
		: state_name(s), condition(p), uses_timer(false), timer_val(0), trigger(0), subcondition_handlers(0), owner(0) { 
		uses_timer = p->usesTimer(timer_val); 
	}

    bool operator<(const StableState &other) const {  // used for std::sort
        if (!condition.predicate || !other.condition.predicate) return false;
        return condition.predicate->priority < other.condition.predicate->priority;
    };
    std::ostream &operator<< (std::ostream &out)const { return out << state_name; }
	StableState& operator=(const StableState* other);
    StableState (const StableState &other);
        
    void setOwner(MachineInstance *m) { owner = m; }
    void fired(Trigger *trigger);
	
	bool uses_timer;
	Value timer_val;
	Trigger *trigger;
	std::list<ConditionHandler> *subcondition_handlers;
    MachineInstance *owner;
};
std::ostream &operator<<(std::ostream &out, const StableState &ss);

struct Parameter {
    Value val;
    SymbolTable properties;
    MachineInstance *machine;
    std::string real_name;
    Parameter(Value v) : val(v), machine(0) { }
    Parameter(const char *name, const SymbolTable &st) : val(name), properties(st), machine(0) { }
    std::ostream &operator<< (std::ostream &out)const { return out << val << "(" << properties << ")"; }
    Parameter(const Parameter &orig) { val = orig.val; machine = orig.machine; properties = orig.properties; }
};
std::ostream &operator<<(std::ostream &out, const Parameter &p);

// modbus interfacing
struct ModbusAddressTemplate {
    //Group - discrete, coil, etc
    //Size - number of units of the address type (always 1 for discretes and coils)
	std::string property_name;
	ModbusAddress::Group kind;
	int size; // number of units
	ModbusAddressTemplate(const std::string &property, ModbusAddress::Group g, int n_units) 
		: property_name(property), kind(g), size(n_units) {	}
};

struct MachineClass {
    MachineClass(const char *class_name);
    SymbolTable properties;
    std::vector<Parameter> parameters;
    std::vector<Parameter> locals;
    std::list<State> states;
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
    virtual ~MachineClass() { all_machine_classes.remove(this); }
	void disableAutomaticStateChanges() { allow_auto_states = false; }
	void enableAutomaticStateChanges() { allow_auto_states = true; }
    State default_state;
	State initial_state;
    std::string name;
    static std::list<MachineClass*> all_machine_classes;
	bool allow_auto_states;
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

class MachineInstance : public Receiver, public ModbusAddressable, public TriggerOwner {
public:
    enum PollType { BUILTINS, NO_BUILTINS};
	enum InstanceType { MACHINE_INSTANCE, MACHINE_TEMPLATE };
    MachineInstance(InstanceType instance_type = MACHINE_INSTANCE);
    MachineInstance(CStringHolder name, const char * type, InstanceType instance_type = MACHINE_INSTANCE);
    virtual ~MachineInstance();
	virtual Receiver *asReceiver() { return this; }
    
    void addParameter(Value param);
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
    virtual void idle();
	void collect(const Package &package);

	std::map<std::string, MachineInstance *> localised_names;
	MachineInstance *lookup_cache_miss(const std::string &seek_machine_name);
    MachineInstance *lookup(Parameter &param);
    MachineInstance *lookup(Value &val);
    MachineInstance *lookup(const char *);
    MachineInstance *lookup(const std::string &name);
    const Value &getValue(std::string property);
    void setValue(std::string property, Value new_value);
    
    void setStateMachine(MachineClass *machine_class);
    bool stateExists(State &s);
    void listenTo(MachineInstance *m);
    void stopListening(MachineInstance *m);
    void setStableState();
    
    //std::string _name;
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

	State &getCurrent() { return current_state; }
	const char *getCurrentStateString() { return current_state.getName().c_str(); }
	
	void addDependancy(MachineInstance *m);
	
	// this depends on machine m if it is in m's list of dependants or if the machine
	// is in this machines listen list.
	bool dependsOn(Transmitter *m);
	
	bool needsCheck();

    static void processAll(PollType which);
	//static void updateAllTimers(PollType which);
	//void updateTimer(long dt);
    static void checkStableStates();
	static int countAutomaticMachines() { return automatic_machines.size(); }
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
    
	MachineClass *getStateMachine() const { return state_machine; }
	void setInitialState() { if (state_machine) setState(state_machine->initial_state); }
	Trigger setupTrigger(const std::string &machine_name, const std::string &message, const char *suffix);
	const Value *getTimerVal();
	const Value *getCurrentStateVal() { return &current_state_val; }

	bool uses(MachineInstance *other);
	std::set<MachineInstance*>depends;
	
	void enable();
	void resume();
	void resume(const std::string &state_name);
	void disable();
	bool enabled() { return is_enabled; }
	void clearAllActions();

	// basic lock functionality 
	bool lock(MachineInstance *requester) { if (locked && locked != requester) return false; else {locked = requester; return true; } }
	bool unlock(MachineInstance *requester) { if (locked != requester) return false; else { locked = 0; return true; } }
	
	// basic modbus interface
	ModbusAddress addModbusExport(std::string name, ModbusAddress::Group g, unsigned int n, ModbusAddressable *owner, ModbusAddress::Source src, const std::string &full_name);
	std::map<std::string, ModbusAddress >modbus_exports;
	std::map<int, std::string>modbus_addresses;
	void setupModbusInterface();
	void setupModbusPropertyExports(std::string property_name, ModbusAddress::Group grp, int size);
	void refreshModbus(std::ostream &out); // update all exported values
	int getModbusValue(ModbusAddress &addr, unsigned int offset, int len);
	void modbusUpdated(ModbusAddress &addr, unsigned int offset, int new_value);
	void exportModbusMapping(std::ostream &out);
	bool isModbusExported() { return modbus_exported != none; }
	
	// error states are outside of the normal processing for a state machine;
	// they cause other processing to halt and trigger receipt of a message: ERROR
	bool inError();
	void setError(int val);
	void resetError();
	void ignoreError();

	// debug messaging
	void setDebug(bool which);

    IOComponent *io_interface;
	MachineInstance *owner;

    SymbolTable properties;
    std::string definition_file;
    int definition_line;
    //struct timeval timer; // time that the current state has been active
	struct timeval start_time; // time the current state started
	struct timeval disabled_time; // time the current state started

	static void sort();

	bool needs_check;
	bool uses_timer;
	
protected:
	InstanceType my_instance_type;
    MoveStateAction *state_change; // this is set during change between stable states
    MachineClass *state_machine;
    State current_state;
	Action::Status setState(State new_state, bool reexecute = false);
	bool is_enabled;
	Value timer_val;
	MachineInstance *locked;
	ModbusAddress modbus_address;
	std::vector<std::string>state_names; // used for mapping modbus offsets to states
	std::vector<std::string>command_names; // used for mapping modbus offsets to states
	ModbusAddressable::ExportType modbus_exported;
	
	int error_state; // error number of the current error if any
	State saved_state; // save state before error
	Value current_state_val;
    bool is_active; // is this machine active or passive?
	
private:
	static std::map<std::string, HardwareAddress> hw_names;
    MachineInstance &operator=(const MachineInstance &orig);
    static std::list<MachineInstance*> all_machines;
    static std::list<MachineInstance*> automatic_machines; // machines with auto state changes enabled
    static std::list<MachineInstance*> active_machines; // machines that require idle() processing

	friend class SetStateAction;
	friend class MoveStateAction;
	friend class SetIOStateAction;
	friend class ExpressionAction;
	friend class IODCommandToggle;
    friend class IODCommandSetStatus;
};

std::ostream &operator<<(std::ostream &out, const MachineInstance &m);


#endif
