#ifndef __MACHINECLASS_H__
#define __MACHINECLASS_H__

#include "Message.h"
#include "ModbusInterface.h"
#include "Parameter.h"
#include "StableState.h"
#include "State.h"
#include "Transition.h"
#include "symboltable.h"
#include "value.h"
#include <list>
#include <map>
#include <set>
#include <string.h>
#include <vector>

// modbus interfacing
class ModbusAddressTemplate {
  public:
    //Group - discrete, coil, etc
    //Size - number of units of the address type (always 1 for discretes and coils)
    std::string property_name;
    ModbusAddress::Group grp;
    ModbusExport::Type exType;
    int size; // number of units
    ModbusAddressTemplate(const std::string &property, ModbusAddress::Group g,
                          ModbusExport::Type kind, int n_units)
        : property_name(property), grp(g), exType(kind), size(n_units) {}
};

class MachineCommandTemplate;
class MachineInstance;
class Plugin;

class MachineClass {
    SymbolTable properties;
    std::map<std::string, Value> options;

  public:
    std::set<std::string> local_properties;
    std::set<std::string> persistent_properties;
    std::set<std::string> private_properties;
    std::vector<Parameter> parameters;
    std::vector<Parameter> locals;
    std::list<State *> states;
    std::set<std::string> state_names;
    std::multimap<std::string, StableState> stable_state_xref;
    std::vector<StableState> stable_states;
    std::list<Predicate *> timer_clauses;
    std::multimap<std::string, MachineCommandTemplate *> commands;
    std::map<Message, MachineCommandTemplate *> enter_functions;
    std::multimap<Message, MachineCommandTemplate *> receives;
    std::map<std::string, MachineInstance *> global_references;
    std::list<Transition> transitions;
    std::list<ModbusAddressTemplate> exports;
    std::vector<std::string> state_exports;
    std::vector<std::string> state_exports_rw;
    std::vector<std::string> command_exports;
    static std::map<std::string, MachineClass*> machine_classes;
    explicit MachineClass(const char *class_name);
    void defaultState(State state);
    bool isStableState(State &state);
    void addState(const char *name, bool is_static = false);
    const State *findState(const char *name) const;
    const State *findState(const State &seek) const;
    bool isStaticState(const char *name);
    bool isStaticState(const std::string &);
    State *findMutableState(const char *name);
    virtual ~MachineClass() { all_machine_classes.remove(this); }
    void disableAutomaticStateChanges();
    void enableAutomaticStateChanges();
    static MachineClass *find(const char *name);
    void collectTimerPredicates();

    const SymbolTable &getProperties() const { return properties; }
    void setProperties(const SymbolTable &props) { properties = props; }
    bool setProperty(const char *name, const Value &val);
    bool setProperty(const std::string name, const Value &val);

    const std::map<std::string, Value> &getOptions() const { return options; }
    void setOption(const std::string &name, const Value &value);

    virtual void addProperty(const char *name); // used in interfaces to list synced properties
    virtual void
    addPrivateProperty(const char *name); // used in interfaces to list synced properties
    virtual void
    addPrivateProperty(const std::string &name); // used in interfaces to list synced properties
    virtual void addCommand(const char *name);   // used in interfaces to list permitted commands
    // PERSISTENT OPTION: a non-column field persisted to persist.dat (only that
    // field). Marked local (not a column) and persistent.
    virtual void addPersistentProperty(const char *name);
    virtual void addPersistentProperty(const std::string &name);
    MachineCommandTemplate *findMatchingCommand(std::string cmd_name, const char *state);
    MachineCommandTemplate *findOverlappingCommand(const std::string &cmd_name,
                                                   const std::vector<std::string> &within,
                                                   const Predicate *guard) const;
    MachineCommandTemplate *findOverlappingReceive(const Message &msg,
                                                   const std::vector<std::string> &within,
                                                   const Predicate *guard) const;
    bool propertyIsLocal(const char *name) const;
    bool propertyIsLocal(const std::string &name) const;
    bool propertyIsLocal(const Value &name) const;
    bool propertyIsPrivate(const char *name) const { return private_properties.count(name) > 0; }
    bool propertyIsPrivate(const std::string &name) const { return private_properties.count(name) > 0; }
    bool propertyIsPrivate(const Value &name) const { return private_properties.count(name.asString()) > 0; }
    void addPrivateColumn(const std::string &name) { private_properties.insert(name); }
    bool propertyIsPersistent(const char *name) const;
    bool propertyIsPersistent(const std::string &name) const;
    bool propertyIsPersistent(const Value &name) const {
        return persistent_properties.count(name.asString()) > 0;
    }
    bool hasPersistentProperties() const { return !persistent_properties.empty(); }

    State default_state;
    State initial_state;
    std::string name;
    static std::list<MachineClass *> all_machine_classes;
    bool allow_auto_states;
    int token_id;
    Plugin *plugin;
    long polling_delay;

    std::set<std::string> property_names;
    std::set<std::string> command_names;

    MachineClass *parent;

    void exportHandlers(std::ostream &ofs);
    void exportCommands(std::ostream &ofs);
    bool cExport(const std::string &filename);

  private:
    std::set<std::string> static_state_names;

    MachineClass();
    MachineClass(const MachineClass &other);
};

#endif
