#ifndef __MACHINECLASS_H__
#define __MACHINECLASS_H__

#include <set>
#include <vector>
#include <list>
#include <map>
#include <string.h>
#include "Message.h"
#include "Parameter.h"
#include "ModbusInterface.h"
#include "symboltable.h"
#include "value.h"
#include "Transition.h"
#include "State.h"
#include "StableState.h"

// modbus interfacing
class ModbusAddressTemplate {
public:
    //Group - discrete, coil, etc
    //Size - number of units of the address type (always 1 for discretes and coils)
	std::string property_name;
	ModbusAddress::Group grp;
	ModbusExport::Type exType;
	int size; // number of units
	ModbusAddressTemplate(const std::string &property, ModbusAddress::Group g, ModbusExport::Type kind, int n_units)
		: property_name(property), grp(g), exType(kind), size(n_units) {	}
};

class MachineCommandTemplate;
class MachineInstance;
class Plugin;

class ExportState {
public:
  const Value &symbol(const char *name);
  const Value &create_symbol(const char *name);
  static void add_state(const std::string name);
  static int lookup(const std::string name);
  static void add_message(const std::string name, int value = -1);
  static std::map<std::string, int> &all_messages() { return message_ids; }
private:
  SymbolTable messages;
  static std::map<std::string, int> string_ids;
  static std::map<std::string, int> message_ids;

};

class MachineClass {
public:
  MachineClass(const char *class_name);
  SymbolTable properties;
  std::set<std::string> local_properties;
  std::vector<Parameter> parameters;
  std::vector<Parameter> locals;
  std::list<State *> states;
  std::set<std::string> state_names;
  std::multimap<std::string, StableState> stable_state_xref;
  std::vector<StableState> stable_states;
  std::list<Predicate *> timer_clauses;
  std::multimap<std::string, MachineCommandTemplate*> commands;
  std::map<Message, MachineCommandTemplate*> enter_functions;
  std::multimap<Message, MachineCommandTemplate*> receives;
	std::map<std::string, MachineInstance*>global_references;
	std::map<std::string, Value> options;
  std::list<Transition> transitions;
  std::list<ModbusAddressTemplate> exports;
  std::vector<std::string> state_exports;
	std::vector<std::string> state_exports_rw;
  std::vector<std::string> command_exports;
  static std::map<std::string, MachineClass> machine_classes;
  void defaultState(State state);
  bool isStableState(State &state);
  void addState(const char *name);
  const State *findState(const char *name) const;
  const State *findState(const State &seek) const;
  State *findMutableState(const char *name);
  virtual ~MachineClass() { all_machine_classes.remove(this); }
  void disableAutomaticStateChanges();
  void enableAutomaticStateChanges();
  static MachineClass *find(const char *name);
  void collectTimerPredicates();

	virtual void addProperty(const char *name); // used in interfaces to list synced properties
	virtual void addPrivateProperty(const char *name); // used in interfaces to list synced properties
	virtual void addPrivateProperty(const std::string &name); // used in interfaces to list synced properties
	virtual void addCommand(const char *name); // used in interfaces to list permitted commands
	MachineCommandTemplate *findMatchingCommand(std::string cmd_name, const char *state);
	bool propertyIsLocal(const char *name) const;
	bool propertyIsLocal(const std::string &name) const;
	bool propertyIsLocal(const Value &name) const;

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

  void exportHandlers(std::ostream &ofs);
	void exportCommands(std::ostream &ofs);
	bool cExport(const std::string &filename);

private:
    MachineClass();
    MachineClass(const MachineClass &other);
};

#endif
