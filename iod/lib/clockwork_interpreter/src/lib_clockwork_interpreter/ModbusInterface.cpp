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
#include <lib_clockwork_client/includes.hpp>
#include "ModbusInterface.h"
// // #include "Logger.h"
// // #include "DebugExtra.h"
// #include "MessagingInterface.h"
#include <iomanip>
#include <utility>
#include "options.h"
#include "MachineInstance.h"
#include "Channel.h"

 int ModbusAddress::next_discrete = ModbusAddress::first_auto_address; // next free address in the group
 int ModbusAddress::next_coil = ModbusAddress::first_auto_address; // next free address in the group
 int ModbusAddress::next_input_register = ModbusAddress::first_auto_address; // next free address in the group
 int ModbusAddress::next_holding_register = ModbusAddress::first_auto_address; // next free address in the group

 std::map<int, ModbusAddress> ModbusAddress::user_mappings;
 std::map<int, ModbusAddress> ModbusAddress::automatic_mappings;

 std::map<std::string, ModbusAddressDetails> ModbusAddress::preset_modbus_mapping;

ModbusAddress::Group groupFromInt(int g) {
	if (g == (int)ModbusAddress::coil) return ModbusAddress::coil;
	if (g == (int)ModbusAddress::discrete) return ModbusAddress::discrete;
	if (g == (int)ModbusAddress::input_register) return ModbusAddress::input_register;
	if (g == (int)ModbusAddress::holding_register) return ModbusAddress::holding_register;
	return ModbusAddress::none;
}

int ModbusAddress::next(ModbusAddress::Group g) {
	switch(g) {
		case discrete: return next_discrete;
		case coil:  return next_coil;
		case input_register:  return next_input_register;
		case holding_register: return next_holding_register;
		default: ;
	}
	return 0;
}

ModbusAddress ModbusAddress::alloc(ModbusAddress::Group g, unsigned int n, ModbusAddressable*ma, ModbusAddress::Source src, const std::string &full_name, ModbusExport::Type kind) {
	if (n == 0) return ModbusAddress();
	int address;
	switch(g) {
		case discrete:
			address = next_discrete; next_discrete+=n; break;
		case coil:
			address = next_coil; next_coil+=n; break;
		case input_register:
			address = next_input_register; next_input_register+=n; break;
		case holding_register:
			address = next_holding_register; next_holding_register+=n; break;
		case string:
			address = next_input_register; next_input_register+=n; break;
		default: return ModbusAddress();
	}
	DBG_MODBUS << "Modbus allocated " << n << " addresses in group " << (int)g << " starting at " << address<< "\n";
	ModbusAddress addr = ModbusAddress(g, address, n, ma, src, full_name, kind, 0);
	int index = ( (int)g <<16) + address;
	if (address < first_auto_address)
		user_mappings[index] = addr;
	else
		automatic_mappings[index] = addr;
	return addr;
}

ModbusAddress::ModbusAddress(ModbusAddress::Group g, int a, int n, ModbusAddressable *o, Source src, const std::string &nam, ModbusExport::Type exType, int i)
	: group(g), address(a), allocated(n), exportType(exType), offset(i), source(src), owner(o), name(nam) {
	switch(g) {
		case discrete:
			if (address >= next_discrete)
				next_discrete=address + 1; break;
		case coil:
			if (address >= next_coil)
				next_coil = address + 1; break;
		case input_register:
			if (address >= next_input_register)
				next_input_register=address + n; break;
		case holding_register:
			if (address >= next_holding_register)
				next_holding_register=address + n; break;
		case string:
			if (address >= next_holding_register)
				next_holding_register=address + n; break;
			default: ;
	}
	DBG_MODBUS << "Modbus recording " << n << " addresses in group " << (int)g << " starting at " << address<< "\n";
	int index = ( (int)g <<16) + address;
	if (address < first_auto_address)
		user_mappings[index] = *this;
	else
		automatic_mappings[index] = *this;
}

bool ModbusAddress::operator==(const ModbusAddress &other) const {
	return group == other.group && address == other.address;
}

void ModbusAddress::message(const std::string &msg) {
}

void ModbusAddress::update(MachineInstance *owner, Group group, int addr, const char *str_value, int len) {
    std::list<Value>params;
    params.push_back(group);
    params.push_back(addr);
    params.push_back(Value(name.c_str(),Value::t_string));
    params.push_back(len);
    params.push_back(Value(str_value,Value::t_string));
	MessageHeader mh(MessageHeader::SOCK_CW, MessageHeader::SOCK_CW, false);
	mh.start_time = microsecs();
	Channel::sendCommand(owner, "UPDATE", &params, mh);
}

void ModbusAddress::update(MachineInstance *owner, Group group, int addr, int new_value, int len) {
    std::list<Value>params;
    params.push_back(group);
    params.push_back(addr);
    params.push_back(Value(name.c_str(),Value::t_string));
    params.push_back(len);
    params.push_back(new_value);
	MessageHeader mh(MessageHeader::SOCK_CW, MessageHeader::SOCK_CW, false);
	mh.start_time = microsecs();
	Channel::sendCommand(owner, "UPDATE", &params, mh);
}

void ModbusAddress::update(MachineInstance *owner, Group group, int addr, double new_value, int len) {
	std::list<Value>params;
	params.push_back(group);
	params.push_back(addr);
	params.push_back(Value(name.c_str(),Value::t_string));
	params.push_back(len);
	params.push_back(new_value);
	MessageHeader mh(MessageHeader::SOCK_CW, MessageHeader::SOCK_CW, false);
	mh.start_time = microsecs();
	Channel::sendCommand(owner, "UPDATE", &params, mh);
}

void ModbusAddress::update(MachineInstance *owner, int index, int new_value, int len) {
	int addr = index & 0xffff;
	int group = index >> 16;
	update(owner, (Group)group, addr, new_value, len);
}

void ModbusAddress::update(MachineInstance *owner, double new_value) {
	update(owner, group, address, new_value, allocated);
}

void ModbusAddress::update(MachineInstance *owner, int new_value) {
	update(owner, group, address, new_value, allocated);
}

void ModbusAddress::update(MachineInstance *owner, const std::string &str) {
	update(owner, group, address, str.c_str(), allocated);
}

// find the Modbus address that has a mapped entry for the given group and address
ModbusAddress ModbusAddress::lookup(int group, int address) {
	std::map<int, ModbusAddress>::iterator search;
	std::map<int, ModbusAddress> *space = &automatic_mappings;
	int index = ( group << 16) + address;


	DBG_MODBUS << "looking up modbus " << group << ":" << address << ". Auto size: " << automatic_mappings.size() << " local mappings " << user_mappings.size() << "\n";
	search = space->find(index);

	if (search == space->end()) {
		space = &user_mappings;
		search = space->find(index);
	}

	//if (address < first_auto_address) space = user_mappings;

	// not found? return a null result
	if (search == space->end()) 	return ModbusAddress();
	return (*search).second;
}

std::ostream &ModbusAddress::operator<<(std::ostream &out) const {
	out <<name <<": " << group << std::setfill('0') << std::setw(5) << address << " src:" << source;
	return out;
}

std::ostream &operator<<( std::ostream &out, const ModbusAddress &addr) {
	return addr.operator<<(out);
}

void ModbusAddressable::updateModbus(int new_value) {

}

ModbusAddressable::~ModbusAddressable() {

}
