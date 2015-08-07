//
// monitor.cpp
// Created by Martin on 4/08/2015.

#include "monitor.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdlib.h>

std::map<unsigned int, ModbusMonitor*> ModbusMonitor::addresses;


ModbusValue::~ModbusValue() {
	
}

ModbusValue::ModbusValue(unsigned int len) : length(len) {
	
}

void ModbusValueBit::set(uint8_t *data) {
	// libmodbus  uses a byte to store each bit
	uint8_t *p = val;
	for (int i = 0; i<length; ++i) *p++ = *data++;
}

void ModbusValueBit::set(uint16_t *data) {
	uint8_t *p = val;
	
	int i = 0;
	while (i++<length) {
		uint16_t x = *data++;
		*p++ = (x & 0xff00) >> 8;
		*p++ = x * 0x00ff;
	}
}

void ModbusValueWord::set(uint8_t *data) {
	uint16_t x = 0;
	for (int i = 0; i<length; ++i) {
		x = *data++;
		x += ( *data++ ) <<8;
		val[i] = x;
	}
}

void ModbusValueWord::set(uint16_t *data) {
	uint16_t *p = val;
	for (int i = 0; i<length; ++i) *p++ = *data++;
}

std::ostream &ModbusMonitor::operator<<(std::ostream &out) const {
	return out << name_ << " " << group_  << ":"
		<< std::setw(4) << std::setfill('0') << address_ << " " << len_ 
		<< " value-length: " << value->length;
}

std::ostream &operator<<(std::ostream &out, const ModbusMonitor &m) {
	return m.operator<<(out);
}


ModbusMonitor::ModbusMonitor(std::string name, unsigned int group, unsigned int address, unsigned int len)
: name_(name), group_(group),address_(address), len_(len), value(0)
{
	if (group_==0 || group_==1)
		value = new ModbusValueBit(len_);
	else if (group_==3 || group_==4)
		value = new ModbusValueWord(len_);
	else
		throw std::exception();
}

ModbusMonitor::ModbusMonitor(const ModbusMonitor &other)
: name_(other.name_), group_(other.group_),address_(other.address_), len_(other.len_), value(0)
{
	if (group_==0 || group_==1) {
		value = new ModbusValueBit(len_);
		set( other.value->getBitData(), false );
	}
	else if (group_==3 || group_==4) {
		value = new ModbusValueWord(len_);
		set( other.value->getWordData(), false );
	}
	else {
		throw std::exception();
	}
}

/*
ModbusMonitor &ModbusMonitor::operator=(const ModbusMonitor &other) 
{
	name_ = other.name_; group_ = other.group_; address_ = other.address_; len_ = other.len_;
	value = 0;
	throw std::exception();
	return *this;
}*/

ModbusMonitor *ModbusMonitor::lookupAddress(unsigned int adr) {
	try {
		ModbusMonitor *mm = addresses.at(adr);
		return mm;
	}
	catch (std::exception ex) {
		std::cout << "no device for address " << adr << "\n";
		return 0;
	}
}

ModbusMonitor *ModbusMonitor::lookup(unsigned int grp, unsigned int adr) {
	return lookupAddress( (grp<<16)+adr );
}

void ModbusMonitor::set(uint8_t *new_value, bool display_value) {
	/*if (length>2) displayAscii(data, length); else*/ 
	if (display_value) { display(new_value, value->length); std::cout << "\n"; }
	//std::cout << " setting " << *this << "\n";
	if (this->value == 0) throw std::exception();
	this->value->set(new_value);
}
void ModbusMonitor::set(uint16_t *new_value, bool display_value) { 
	/*if (length>2) displayAscii(data, length); else*/ 
	if (display_value) { display((uint8_t *)new_value, value->length * sizeof(uint16_t)); std::cout << "\n"; }
	//std::cout << " setting " << *this << "\n";
	if (value == 0) throw std::exception();
	value->set(new_value);
}		

bool MonitorConfiguration::load(const char *fname) {
	std::ifstream in(fname);
	while (!in.eof()) {
		std::string code;
		std::string name;
		std::string kind;
		unsigned int group;
		unsigned int address;
		unsigned int len;
		in >> code >> name >> kind >> len;
		if (in.good()) {
			group = code[0] - '0';
			char *rest = 0;
			address = strtol(code.c_str()+2, &rest, 10);
			ModbusMonitor *m = new ModbusMonitor(name, group, address, len);
			monitors.insert(make_pair(name, *m));
			delete m;
		}
		else if (!in.eof()) return false;
	}
	
	return true;
}


void ModbusMonitor::add() {
	unsigned int adr = (group_ <<16) + address_;
	for (int i=0; i<len_; ++i) {
		//std::cout << "adding address: " << (adr+i) << " for " << *this << "\n";
		addresses[adr+i] = this;
	}
}
/*
void ModbusMonitor::showAddresses() {
	std::map<unsigned int, ModbusMonitor*>::iterator iter = ModbusMonitor::addresses.begin();
	while (iter != ModbusMonitor::addresses.end()) {
		std::cout << *(*iter++).second << "\n";
	}
}
*/

