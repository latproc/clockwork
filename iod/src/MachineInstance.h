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

#include "Action.h"
#include "ActionList.h"
#include "ConditionHandler.h"
#include "Expression.h"
#include "CommandClock.h"
#include "MachineClass.h"
#include "MachineCommandAction.h"
#include "Message.h"
#include "ModbusInterface.h"
#include "Parameter.h"
#include "Plugin.h"
#include "Receiver.h"
#include "SetStateAction.h"
#include "StableState.h"
#include "State.h"
#include "Statistic.h"
#include "Transition.h"
#include "dynamic_value.h"
#include "symboltable.h"
#include <array>
#include <boost/foreach.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <cassert>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <sys/time.h>
#include <vector>
#include "ThreadSafeList.h"

extern SymbolTable globals;

class MachineInstance;
class MachineClass;
struct MoveStateAction;
class IOComponent;
class MQTTModule;
struct cJSON;
class Channel;

extern std::map<std::string, MachineInstance *> machines;

struct HardwareAddress {
    int io_offset;
    int io_bitpos;
    HardwareAddress(int offs, int bitp) : io_offset(offs), io_bitpos(bitp) {}
    HardwareAddress() : io_offset(0), io_bitpos(0) {}
};

class MachineInstance : public Receiver, public ModbusAddressable, public TriggerOwner {
    friend class MachineInstanceFactory;

  public:
    enum PollType { BUILTINS, NO_BUILTINS };
    enum InstanceType { MACHINE_INSTANCE, MACHINE_TEMPLATE, MACHINE_SHADOW };

    struct StateHistoryEntry {
        std::string state_name;
        uint64_t entered_at = 0; // microsecond timestamp when state was entered
        uint64_t left_at = 0;    // microsecond timestamp when state was left
    };
    static constexpr size_t STATE_HISTORY_SIZE = 32;

    /** Summary of recent completed state visits (from state_history).
     *  Flags continuous fast thrash only — not "long stable + brief test hop". */
    struct StateThrashInfo {
        size_t history_count = 0;
        size_t short_holds = 0;     // holds shorter than short_hold_us
        uint64_t min_hold_us = 0;
        uint64_t max_hold_us = 0;
        uint64_t avg_hold_us = 0;
        uint64_t span_us = 0;       // oldest enter → newest leave (ring wall clock)
        uint64_t current_dwell_us = 0; // time in current state so far
        double transitions_per_sec = 0.0;
        bool thrashing = false;     // continuous rapid flip-flop
        std::string path;           // e.g. a -> b -> a -> b (oldest to newest)
    };
    /** On-demand only (DESCRIBE / SHOW CYCLING) — not used in the processing loop.
     *  short_hold_us: hold shorter than this counts as "short" (default 50ms).
     *  min_short: need at least this many short holds AND almost-all-short fraction.
     *  want_path: build path string only when thrashing (cheap when scanning plant).
     *  Cost is O(STATE_HISTORY_SIZE) ≈ 8. */
    StateThrashInfo analyseStateThrash(uint64_t short_hold_us = 50000, size_t min_short = 5,
                                       bool want_path = true) const;

  protected:
    MachineInstance(InstanceType instance_type = MACHINE_INSTANCE);
    MachineInstance(CStringHolder name, const char *type,
                    InstanceType instance_type = MACHINE_INSTANCE);

    class SharedCache;
    class Cache;
    static ThreadSafeList<MachineInstance *> to_remove;

  public:
    virtual ~MachineInstance();
    virtual Receiver *asReceiver() { return this; }

    static void prepare_to_remove(MachineInstance *m) { to_remove.push_back(m); }
    static void remove_pending();

    void triggerFired(Trigger *trig);

    void addParameter(const Parameter &param, MachineInstance *machine = 0, ssize_t position = -1,
                      bool before = false);
    void addParameter(Value param, MachineInstance *machine = 0, ssize_t position = -1,
                      bool before = false);
    void removeParameter(size_t which);
    void addLocal(Value param, MachineInstance *machine = 0);
    void removeLocal(int index);
    void setProperties(const SymbolTable &props);

    // record where in the program this machine was defined
    void setDefinitionLocation(const char *file, int line_no);

    // tbd record which parts of the program refer to this machine
    //void addReferenceLocation(const char *file, int line_no);

    virtual std::ostream &operator<<(std::ostream &out) const;
    void describe(std::ostream &out);

    static void add_io_entry(const char *name, unsigned int io_offset, unsigned int bit_offset);

    virtual bool receives(const Message &, Transmitter *t);
    Action::Status execute(const Message &m, Transmitter *from, Action *action = 0);
    virtual void handle(const Message &, Transmitter *from, bool send_receipt = false);
    virtual void sendMessageToReceiver(const Message &m, Receiver *r = NULL,
                                       bool expect_reply = false);
    virtual void sendMessageToReceiver(const char *msg, Receiver *r = NULL,
                                       bool expect_reply = false);

    virtual void idle();
    //virtual bool hasWork() { return has_work; }
    void collect(const Package &package);

    std::map<std::string, MachineInstance *> localised_names;
    MachineInstance *lookup_cache_miss(const std::string &seek_machine_name);
    MachineInstance *lookup(Parameter &param);
    MachineInstance *lookup(Value &val);
    MachineInstance *lookup(const char *);
    MachineInstance *lookup(const std::string &name);
    Value *getMutableValue(const char *prop);
    const Value &getValue(
        const char *
            prop); // provides the current value of an object accessible in the scope of this machine
    const Value &getValue(
        const std::string &
            property); // provides the current value of an object accessible in the scope of this machine
    const Value *getValuePtr(
        Value &
            property); // provides the current value of an object accessible in the scope of this machine
    const Value &getValue(
        Value &
            property); // provides the current value of an object accessible in the scope of this machine
    virtual bool setValue(const std::string &property, const Value &new_value,
                          uint64_t authority = 0);
    const Value *resolve(
        std::string
            property); // provides a pointer to the value of an object that can be evaluated in the future

    void setStateMachine(MachineClass *machine_class);
    bool stateExists(State &s);
    bool hasState(const State &s) const;
    bool hasState(const std::string &state_name) const;
    const Value *lookupState(const std::string &state_name);
    const Value *lookupState(const Value &);
    void listenTo(MachineInstance *m);
    void stopListening(MachineInstance *m);
    bool isStableState(const std::string state_name);
    std::string firstValidStableState(const std::string state_name);
    bool setStableState();   // returns true if a state change was made
    virtual bool isShadow(); // is this machine a shadow instance?
    virtual Channel *ownerChannel();

    std::string &fullName() const;
    std::string modbusName(const std::string &property, const Value &property_val);
    std::string _type;
    std::vector<Parameter> parameters;
    std::vector<Parameter> locals;
    std::vector<StableState> stable_states;
    std::multimap<std::string, MachineCommand *> commands;
    std::map<Message, MachineCommand *> enter_functions;
    std::multimap<Message, MachineCommand *> receives_functions;
    std::set<Transmitter *> listens;
    std::list<Transition> transitions;

    Action *executingCommand(); // returns the action currently executing
    ActionList active_actions;
    void displayActive(std::ostream &out);
    void start(Action *a);
    void stop(Action *a);
    void push(Action *new_action);
    void prepareCompletionMessage(Transmitter *from, std::string message);
    Action *findHandler(Message &msg, Transmitter *t, bool response_required = false);
    void enqueueAction(Action *a);
    void enqueue(const Package &package);
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

    /**
     * Careful fan-out: SEND command only to dependants that declare that command
     * (RECEIVE/COMMAND). Does not setNeedsCheck the whole depends tree.
     * Default "update" for AI/COUNTER (CLOCKEDANALOGINPUT etc.).
     *
     * notify_period_ms: coalesce window for hasPending (skip if a matching
     * command is already in mail and younger than 2×period, min 50 ms). Stale
     * pending is dropped and one replacement is enqueued (per dependant).
     */
    void notifyCommandConsumers(const char *command_name, uint64_t notify_period_ms = 100);
    // True if a RECEIVE/COMMAND handler for command_name applies now
    // (WITHIN list, WHEN/REQUIRES, unrestricted fallback). Same rules as
    // tests/unit/command_guards.cw. No handler → false.
    bool acceptsCommandInCurrentState(const char *command_name) const;
    void notifyClockedUpdateConsumers(uint64_t notify_period_ms = 100) {
        notifyCommandConsumers("update", notify_period_ms);
    }

    bool needsCheck();
    void resetNeedsCheck();
    /** LIST only: default true. When false (property propagate_member_checks),
     *  member state/property noise does not cascade setNeedsCheck to LIST
     *  dependents; enable/disable of members still does. */
    bool listPropagatesMemberChecks() const;
    void propagateNeedsCheckToDependents();
    void resetTemporaryStringStream();

    static bool processAll(std::set<MachineInstance *> &to_process, uint32_t max_time,
                           PollType which);
    //static void updateAllTimers(PollType which);
    //void updateTimer(long dt);
    static bool checkStableStates(std::set<MachineInstance *> &to_process, uint32_t max_time);
    static void checkPluginStates();
    static size_t countAutomaticMachines() { return automatic_machines.size(); }
    static void displayAutomaticMachines();
    static void displayAll();

    static MachineInstance *find(const char *name);
    static std::list<MachineInstance *>::iterator begin() { return all_machines.begin(); }
    static std::list<MachineInstance *>::iterator end() { return all_machines.end(); }

    static std::list<MachineInstance *>::iterator io_modules_begin() { return io_modules.begin(); }
    static std::list<MachineInstance *>::iterator io_modules_end() { return io_modules.end(); }

    static std::list<MachineInstance *>::iterator begin_active() { return active_machines.begin(); }
    static std::list<MachineInstance *>::iterator end_active() { return active_machines.end(); }
    static void addActiveMachine(MachineInstance *m) { active_machines.push_back(m); }
    void markActive();
    void markPassive();
    void markPlugin();

    bool isPersistent();

    MachineClass *getStateMachine() const { return state_machine; }
    void setInitialState(bool resume = false);
    Trigger *setupTrigger(const std::string &machine_name, const std::string &message,
                          const char *suffix);
    const Value *getTimerVal();
    Value *getCurrentStateVal() { return &current_state_val; }
    Value *getCurrentValue() { return &current_value_holder; }

    bool uses(MachineInstance *other);
    std::set<MachineInstance *> depends;

    virtual void enable();
    virtual void resume();
    void resume(const State &state);
    void resumeAll(); // resume all disable sub-machines
    virtual void disable();
    inline bool enabled() const { return is_enabled; }
    void clearAllActions();
    void checkActions();
    Action *findMatchingCommand(const std::string &command_name);

    // basic lock functionality
    bool lock(MachineInstance *requester) {
        if (locked && locked != requester) {
            return false;
        }
        else {
            locked = requester;
            return true;
        }
    }
    bool unlock(MachineInstance *requester) {
        if (locked != requester) {
            return false;
        }
        else {
            locked = 0;
            return true;
        }
    }
    MachineInstance *locker() const { return locked; }

    // change batching
    bool changing();
    bool prepare();
    void commit();
    void discard();

    // basic modbus interface
    void sendModbusUpdate(const std::string &property_name, const Value &new_value);
    ModbusAddress addModbusExport(std::string name, ModbusAddress::Group g, unsigned int n,
                                  ModbusAddressable *owner, ModbusExport::Type kind,
                                  ModbusAddress::Source src, const std::string &full_name);
    std::map<std::string, ModbusAddress> modbus_exports;
    std::map<int, std::string> modbus_addresses;
    void setupModbusInterface();
    void setupModbusPropertyExports(std::string property_name, ModbusAddress::Group grp,
                                    ModbusExport::Type, int size);
    void refreshModbus(cJSON *json_array); // update all exported values
    int getModbusValue(ModbusAddress &addr, unsigned int offset, int len);
    void modbusUpdated(ModbusAddress &addr, unsigned int offset, int new_value);
    void modbusUpdated(ModbusAddress &addr, unsigned int offset, float new_value);
    void modbusUpdated(ModbusAddress &addr, unsigned int offset, const char *new_value, Value::Kind kind);
    void exportModbusMapping(std::ostream &out);
    bool isModbusExported() { return modbus_exported != ModbusExport::none; }
    bool needsThrottle();
    void setNeedsThrottle(bool which);
    void requireAuthority(uint64_t auth);
    uint64_t requiredAuthority();

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
    uint64_t start_time;    // time the current state started
    uint64_t disabled_time; // time the current state started

    // Circular buffer of recently completed state visits, most recently written at index
    // (state_history_head - 1 + STATE_HISTORY_SIZE) % STATE_HISTORY_SIZE.
    std::array<StateHistoryEntry, STATE_HISTORY_SIZE> state_history;
    size_t state_history_head = 0;  // next write position
    size_t state_history_count = 0; // number of valid entries (capped at STATE_HISTORY_SIZE)
    void pushStateHistory(const std::string &state_name, uint64_t entered_at, uint64_t left_at);

    static void sort();

    virtual void setNeedsCheck();
    Value earliestScheduleTime(const std::list<Predicate *> &predicates);
    uint64_t lastStateEvaluationTime() { return last_state_evaluation_time; }
    uint64_t lastStateEvaluationAge() const {
        return (last_state_evaluation_time && microsecs() > last_state_evaluation_time)
                   ? microsecs() - last_state_evaluation_time : 0;
    }
    uint64_t currentStateAge() const {
        return (start_time && microsecs() > start_time) ? microsecs() - start_time : 0;
    }
    void updateLastEvaluationTime();

    bool queuedForStableStateTest();

    virtual int64_t filter(int64_t val) { return val; }

    void publish();
    void unpublish();

    static void forceStableStateCheck();
    static void forceIdleCheck();
    // Dispatch COMMANDCLOCK instances whose monotonic cadence is due.
    // Called once from the regular EtherCAT input sampling path.
    static void dispatchCommandClocks(uint64_t now_us);
    static bool workToDo();
    static std::list<Package *> &pendingEvents();
    static std::set<MachineInstance *> &pluginMachines();

  protected:
    int needs_check;

  public:
    bool uses_timer;

  protected:
    InstanceType my_instance_type;
    MoveStateAction *state_change; // this is set during change between stable states
    MachineClass *state_machine;
    State current_state;
    uint64_t setupSubconditionTriggers(const StableState &s, uint64_t earliestTimer);
    Action *findReceiveHandler(Transmitter *from, const Message &m, const std::string short_name,
                               bool response_required);
    virtual Action::Status setState(const State &new_state) {
        return setState(new_state, 0, false);
    }
    virtual Action::Status setState(const char *new_state) { return setState(new_state, 0, false); }
    virtual Action::Status setState(const State &new_state, uint64_t authority, bool resume);
    virtual Action::Status setState(const char *new_state, uint64_t authority, bool resume);
    bool is_enabled;
    Value state_timer;
    MachineInstance *locked;
    ModbusAddress modbus_address;
    std::vector<std::string> state_names;   // used for mapping modbus offsets to states
    std::vector<std::string> command_names; // used for mapping modbus offsets to states
    ModbusExport::Type modbus_exported;

    int error_state;   // error number of the current error if any
    State saved_state; // save state before error
    Value current_state_val;
    bool is_active; // is this machine active or passive?
    CommandClock command_clock;
    Value current_value_holder;
    std::stringstream ss;                // saves recreating string stream for temporary use
    uint64_t last_state_evaluation_time; // dynamic value check against this before recalculating
  public:
    Statistic stable_states_stats;
    Statistic message_handling_stats;
    void *data;         // plugin data
    uint64_t idle_time; // amount of time to be idle between state polls (microsec)
    uint64_t next_poll;

    static Value *polling_delay;
    Value is_traceable;
    int published;
    static SharedCache *shared;
    unsigned int action_errors;
    bool is_changing;
    Trigger *trigger;
    std::list<Predicate *> timer_predicates;
    Channel *owner_channel;

  private:
    Cache *cache;
    static std::map<std::string, HardwareAddress> hw_names;
    MachineInstance &operator=(const MachineInstance &orig);
    MachineInstance(const MachineInstance &other);
    static std::list<MachineInstance *> all_machines;

  protected:
    static std::list<MachineInstance *>
        automatic_machines;                              // machines with auto state changes enabled
    static std::list<MachineInstance *> active_machines; // machines that require idle() processing
    static std::list<MachineInstance *> shadow_machines; // machines that shadow remote machines
    static std::set<MachineInstance *>
        pending_state_change; // machines that need to check their stable states
    static std::set<MachineInstance *> plugin_machines; // machines that have plugins
    static std::list<MachineInstance *> io_modules;     // machines of type MODULE
    static std::list<Package *> pending_events;         // machines that shadow remote machines
    static unsigned int num_machines_with_work;
    static unsigned int total_machines_needing_check;
    uint64_t expected_authority;

    std::map<std::string, Value> changes;

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
    friend class UpdateListItemsAction;
    friend void fixListState(MachineInstance &list);
    friend void initialiseOutputs();
    friend void reapplyOutputDefaults();

    friend int changeState(void *s, const char *new_state);
};

std::ostream &operator<<(std::ostream &out, const MachineInstance &m);

class MachineValue : public DynamicValue {
  public:
    MachineValue(MachineInstance *mi, std::string name) : machine_instance(mi), local_name(name) {}
    void setMachineInstance(MachineInstance *mi) { machine_instance = mi; }
    Value &operator()(MachineInstance *m) {
        if (!machine_instance) {
            machine_instance = m->lookup(local_name);
        }
        if (!machine_instance) {
            last_result = SymbolTable::Null;
            return last_result;
        }
        if (machine_instance->_type == "VARIABLE" || machine_instance->_type == "CONSTANT") {
            last_result = machine_instance->getValue("VALUE");
            return last_result;
        }
        else {
            last_result = *machine_instance->getCurrentStateVal();
            return last_result;
        }
    }
    std::ostream &operator<<(std::ostream &out) const;
    DynamicValue *clone() const;
    virtual void flushCache();

  protected:
    MachineInstance *machine_instance;
    std::string local_name;
};

class MachineInstanceFactory {
  public:
    static MachineInstance *
    create(MachineInstance::InstanceType instance_type = MachineInstance::MACHINE_INSTANCE);
    static MachineInstance *
    create(CStringHolder name, const std::string &type,
           MachineInstance::InstanceType instance_type = MachineInstance::MACHINE_INSTANCE);
};

#endif
