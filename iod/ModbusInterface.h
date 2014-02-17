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

#ifndef __MODBUSINTERFACE_H__
#define __MODBUSINTERFACE_H__ 1

#include <map>
#include <list>
#include <iostream>

/* when a Modbus addressable object changes value, it informs the interface by calling update()
	
*/
class ModbusAddressDetails {
public:
	short group;
	short address;
	short len;
	ModbusAddressDetails(short grp, short addr, short n) : group(grp), address(addr), len(n) {}
	ModbusAddressDetails() : group(0), address(0), len(0) {}
};

class ModbusAddressable;
class ModbusAddress {
public:
	enum Group {coil, discrete, none, input_register, holding_register, string };
	enum Source {unknown, machine, state, command, property};
	static Group toGroup(int g) { if (g<0 || g>4) return none; else return (Group)g; }
	
	static const int first_auto_address = 1000;

	bool operator==(const ModbusAddress &other) const;
	
	ModbusAddress() : group(none), address(0), allocated(0), offset(0),source(unknown) {}
	ModbusAddress(Group group, int address, int len, ModbusAddressable *owner, Source source, const std::string &name, int offset = 0);
	
	static ModbusAddress lookup(int, int address);

	ModbusAddressable *getOwner() { return owner;}	
	Group getGroup() const { return group; }
	int getAddress() const { return address; }
	int getBaseAddress() const { return address - offset;}
	int getOffset() const { return offset; }
	int length() const { return allocated; }
	Source getSource() const { return source; }
	void setName(const std::string &n) { name = n; }
	const std::string &getName() const { return name; }
	
	static int next(Group g);
	static ModbusAddress alloc(Group g, unsigned int n, ModbusAddressable *owner, Source src, const std::string &full_name);
	
	void update(Group g, int addr, const char *value, int len);
	void update(Group g, int addr, int new_value, int len);
	void update(int offset, int new_value, int len);
	void update(int new_value);
	void update(const std::string &value);
	
	bool operator<(const ModbusAddress &other) const {
		if (group < other.group) return true;
		if (group == other.group && address < other.address) return true;
		return false;
	}
	
	std::ostream &operator<<(std::ostream &out) const ;
	
	static int nextDiscrete() { return next_discrete; }
	static int nextCoil() { return next_coil; }
	static int nextInputRegister() { return next_input_register; }
	static int nextHoldingRegister() { return next_holding_register; }
	
	static void setNextDiscrete(int n) { next_discrete = n; }
	static void setNextCoil(int n) { next_coil = n; }
	static void setNextInputRegister(int n) { next_input_register = n; }
	static void setNextHoldingRegister(int n) { next_holding_register = n; }
	
	static std::map<std::string, ModbusAddressDetails> preset_modbus_mapping;

	static void message(const std::string &m);
	
private:	
	Group group;
	int address;
	int allocated;
	int offset;
	Source source;
	
	ModbusAddressable *owner;
	std::string name;
	
	static int next_discrete; // next free address in the group
	static int next_coil; // next free address in the group
	static int next_input_register; // next free address in the group
	static int next_holding_register; // next free address in the group
	
	static std::map<int, ModbusAddress> user_mappings;	
	static std::map<int, ModbusAddress> automatic_mappings;
};

std::ostream &operator<<( std::ostream &out, const ModbusAddress&addr);

ModbusAddress::Group groupFromInt(int g);

class ModbusAddressable {
public:
	enum ExportType {none, discrete, coil, reg, reg32, rw_reg, rw_reg32, str};
	virtual ~ModbusAddressable();
	void updateModbus(int new_value);
	virtual void modbusUpdated(ModbusAddress &addr, unsigned int offset, int new_value) {} // sent to the owner when the interface changes a value
	virtual void modbusUpdated(ModbusAddress &addr, unsigned int offset, const char *new_value) {} // sent to the owner when the interface changes a value
	virtual int getModbusValue(ModbusAddress &addr, unsigned int offset, int len) = 0;
};

class ModbusInterface {

private:
	ModbusInterface() { }
};


#endif
