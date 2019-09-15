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

#include <iostream>
#include <lib_clockwork_interpreter/includes.hpp>
#include "includes.hpp"
// #include "cControlSystemMachine.h"
// #include "IOComponent.h"
// #include "ECInterface.h"
// #include "IOComponent.h"
// #include "ControlSystemMachine.h"
// #include <LatprocConfig.h>
// #include "zmq.h"
// #include "SetStateAction.h"
// #include "MachineInstance.h"

ControlSystemMachine::ControlSystemMachine() : state(s_unknown), ethercat_machine(0),
		activate_requested(false), deactivate_requested(false), ecat_last_synced(0) {
	std::cout << "ControlSystem version "
		<< Latproc_VERSION_MAJOR << "." << Latproc_VERSION_MINOR << "\n";
	int major, minor, patch;
	zmq_version (&major, &minor, &patch);
	std::cout << "ZMQ version " << major << "." << minor << "." << patch << "\n";

	ethercat_machine = MachineInstance::find("ETHERCAT");
}

    /* states */
bool ControlSystemMachine::ready() const { return state == s_operational; }
bool ControlSystemMachine::connected()  const { return state == s_connected || state == s_operational; }


void ControlSystemMachine::requestActivation(bool which) {
	activate_requested = which;
	deactivate_requested = false;
}
bool ControlSystemMachine::activationRequested() { return activate_requested; }
void ControlSystemMachine::requestDeactivation(bool which) {
	deactivate_requested = which;
	activate_requested = false;
}
bool ControlSystemMachine::deactivationRequested() { return deactivate_requested; }

uint64_t ControlSystemMachine::lastUpdated() {
	return ecat_last_synced;
}

#ifdef EC_SIMULATOR
/* conditions */
bool ControlSystemMachine::c_connected() const { return true; }
bool ControlSystemMachine::c_disconnected() const { return false; }
bool ControlSystemMachine::c_online() const { return true; }
bool ControlSystemMachine::c_operational() const { return true; }
bool ControlSystemMachine::c_active() const { return true; }
#else
/* conditions */
bool ControlSystemMachine::c_connected() const {
	return ECInterface::master_state.link_up;
}
bool ControlSystemMachine::c_disconnected() const {
	return !c_connected();
}

bool ControlSystemMachine::c_operational() const {
	return c_connected() && ECInterface::instance()->operational();
}

bool ControlSystemMachine::c_online() const {
	return c_connected() && ECInterface::instance()->online();
}
bool ControlSystemMachine::c_active() const {
	return c_connected() && ECInterface::active;
}
#endif

void ControlSystemMachine::idle() {

	uint64_t msc = ECInterface::master_state_changed;
	unsigned int als = ECInterface::master_state.al_states;
/*
	if (ecat_last_synced == ECInterface::master_state_changed
		&& ecat_last_state == ECInterface::master_state.al_states)
		return;
*/
	ecat_last_synced = msc;
	ecat_last_state = als;
	if (!ethercat_machine)
		ethercat_machine = MachineInstance::find("ETHERCAT");
	switch(state) {
		case s_unknown:
			if (c_connected()) enter_connected();
			if (c_disconnected()) enter_disconnected();
			break;
		case s_connected:
			if (c_online()) enter_slaves_online();
			else if (c_operational()) enter_operational();
			else if (c_disconnected()) enter_disconnected();
			else if (!c_connected()) enter_unknown();
			break;
		case s_slaves_online:
			if (c_active())
				enter_operational();
			else if (!c_online()) {
				if (c_connected()) enter_connected();
				else if (c_disconnected()) enter_disconnected();
				else enter_unknown();
			}
			break;
		case s_operational:
			if (!c_active()) {
				if (c_online()) enter_slaves_online();
				else if (c_connected()) enter_connected();
				else if (c_disconnected()) enter_disconnected();
				else enter_unknown();
			}
			break;
		case s_disconnected:
			if (c_connected()) enter_connected();
			else if (!c_disconnected()) enter_unknown();
	}
	sync_slaves();
}

void ControlSystemMachine::sync_slaves() {
#ifndef EC_SIMULATOR
	std::list<MachineInstance *>::iterator iter = MachineInstance::io_modules_begin();
	while (iter != MachineInstance::io_modules_end()) {
		MachineInstance *m = *iter++;
		if (m->enabled()) {
			const Value &pos = m->getValue("position");
			if (pos != SymbolTable::Null) {
				int position = (int) pos.iValue;
				ECModule *module = ECInterface::findModule(position);
				if (module) {
					const char *module_state = 0;
					int state = module->slave_config_state.al_state & 0x0f; // mask off error bit
					if (state == 1) module_state = "INIT";
					else if (state == 2) module_state = "PREOP";
					else if (state == 3) module_state = "BOOT";
					else if (state == 4) module_state = "SAFEOP";
					else if (state == 8) module_state = "OP";
					if (module_state && m->getCurrent().getName() != module_state) {
						SetStateActionTemplate ssat = SetStateActionTemplate("SELF", module_state);
						SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(m));
						m->enqueueAction(ssa);
					}
				}
			}
		}
		else
			ecat_last_synced = 0; // haven't synced all modules yet because this one is disabled
	}

#endif
}

void ControlSystemMachine::enter_connected() {
	state = s_connected;
	ecat_last_synced = 0; // state changes requires followup sync
	std::cout << "Control System is connected\n ";
	if (ethercat_machine) {
		{
		SetStateActionTemplate ssat = SetStateActionTemplate("ETHERCAT_LS", "UP");
		SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_machine));
		ethercat_machine->enqueueAction(ssa);
		}
		SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "CONNECTED");
		SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_machine));
		ethercat_machine->enqueueAction(ssa);
	}
}

void ControlSystemMachine::enter_disconnected() {
	state = s_disconnected;
	ecat_last_synced = 0; // state changes requires followup sync
	std::cout << "Control System is disconnected "
#ifndef EC_SIMULATOR
		<< " wc_state: 0x"
		<< std::hex << (int)ECInterface::domain1_state.wc_state << std::dec
		<< " link up: "
		<< ECInterface::master_state.link_up
#endif
		<< "\n";
	if (ethercat_machine) {
		{
		SetStateActionTemplate ssat = SetStateActionTemplate("ETHERCAT_LS", "DOWN");
		SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_machine));
		ethercat_machine->enqueueAction(ssa);
		}
		SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "DISCONNECTED");
		SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_machine));
		ethercat_machine->enqueueAction(ssa);
	}
}

void ControlSystemMachine::enter_slaves_online() {
	state = s_slaves_online;
	ecat_last_synced = 0; // state changes requires followup sync
	std::cout << "Control System slaves are online\n ";

	if (ethercat_machine) {
		SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "CONFIG");
		SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_machine));
		ethercat_machine->enqueueAction(ssa);
	}
}

void ControlSystemMachine::enter_operational() {
	state = s_operational;
	ecat_last_synced = 0; // state changes requires followup sync
	std::cout << "Control System is operational\n ";
#ifndef EC_SIMULATOR
	//if (!ECInterface::active) ECInterface::instance()->activate();
#endif
	if (ethercat_machine) {
		SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "ACTIVE");
		SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_machine));
		ethercat_machine->enqueueAction(ssa);
	}
}

void ControlSystemMachine::enter_unknown() {
	state=s_unknown;
	ecat_last_synced = 0; // state changes requires followup sync
	std::cout << "Control System is in an unknown state. "
#ifndef EC_SIMULATOR
		<< " wc_state: 0x"
		<< std::hex << (int)ECInterface::domain1_state.wc_state << std::dec
		<< " link up: "
		<< ECInterface::master_state.link_up
#endif
		<< "\n ";
}
