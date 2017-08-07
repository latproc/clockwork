
#include <list>
#include <vector>
#include "State.h"
#include "StableState.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "MachineCommandAction.h"
#include "Parameter.h"
#include "ModbusInterface.h"

std::list<MachineClass*> MachineClass::all_machine_classes;
std::map<std::string, MachineClass> MachineClass::machine_classes;


MachineClass::MachineClass(const char *class_name) : default_state("unknown"), initial_state("INIT"),
	name(class_name), allow_auto_states(true), token_id(0), plugin(0),
	polling_delay(0)
{
	addState("INIT");
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
	return stable_state_xref.find(state.getName()) != stable_state_xref.end();
}

void MachineClass::addState(const State &state) {
	StateMap::iterator found = states.find(state.getName());
	if (found == states.end())
		states[state.getName()] = new State(state);
	else
		*((*found).second) = state;
}
void MachineClass::addState(const char *name) {
	if (states.find(name) == states.end())
		states[name] = new State(name);
}

State *MachineClass::findMutableState(const char *seek) {
	StateMap::iterator found = states.find(seek);
	if (found != states.end())
		return (*found).second;

	/*
	std::list<State *>::const_iterator iter = states.begin();
	while (iter != states.end()) {
		State *s = *iter++;
		if (s->getName() == seek) return s;
	}
	 */
	return 0;
}

const State *MachineClass::findState(const char *seek) const {
	StateMap::const_iterator found = states.find(seek);
	if (found != states.end())
		return (*found).second;
	return 0;
}

const State *MachineClass::findState(const State &seek) const {
	StateMap::const_iterator found = states.find(seek.getName());
	if (found != states.end())
		return (*found).second;
	return 0;
}

MachineClass *MachineClass::find(const char *seek) {
	int token = Tokeniser::instance()->getTokenId(seek);
	std::list<MachineClass *>::iterator iter = all_machine_classes.begin();
	while (iter != all_machine_classes.end()) {
		MachineClass *mc = *iter++;
		if (mc->token_id == token) return mc;
	}
	return 0;
}

void MachineClass::addProperty(const char *p) {
	property_names.insert(p);
}

void MachineClass::addPrivateProperty(const char *p) {
	property_names.insert(p);
	local_properties.insert(p); //
}

void MachineClass::addPrivateProperty(const std::string &p) {
	property_names.insert(p.c_str());
	local_properties.insert(p); //
}

void MachineClass::addCommand(const char *p) {
	command_names.insert(p);
}

bool MachineClass::propertyIsLocal(const char *name) const {
	return local_properties.count(name) > 0;
}

bool MachineClass::propertyIsLocal(const std::string &name) const {
	return local_properties.count(name) > 0;
}

bool MachineClass::propertyIsLocal(const Value &name) const {
	return local_properties.count(name.asString()) > 0;
}

MachineCommandTemplate *MachineClass::findMatchingCommand(std::string cmd_name, const char *state) {
	// find the correct command to use based on the current state
	std::multimap<std::string, MachineCommandTemplate*>::iterator cmd_it = commands.find(cmd_name);
	while (cmd_it != commands.end()) {
		std::pair<std::string, MachineCommandTemplate*>curr = *cmd_it++;
		if (curr.first != cmd_name) break;
		MachineCommandTemplate *cmd = curr.second;
		if ( !state || cmd->switch_state || (!cmd->switch_state && cmd->getStateName().get() == state)  )
			return cmd;
	}
	return 0;
}
