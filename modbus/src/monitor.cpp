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

ModbusValue::ModbusValue(unsigned int len, const std::string &format) : length(len), format_(format) {
	
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

uint8_t *ModbusValueBit::getBitData() { return val; }

uint16_t *ModbusValueBit::getWordData() { return 0; }

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
	long res = 0;
	if (format() == "BCD") {
		*p = 0;
		for (int i = length; i>0; ) {
			long tmp = 0;
			--i;
			uint16_t x = data[i];
			tmp = ((0xf0 & x)>>4) * 10 + (0x0f & x);
			tmp = tmp * 100 + ( ((0xf000 & x)>>12) * 10 + ((0x0f00 & x)>>8) );
			res = res * 10000 + tmp;
		}	
	}
	else {
		for (int i = length; i>0; ) {
			--i;
			uint16_t x = ((data[i] & 0xff) << 8) + (data[i] >> 8) ;
			res = (res << 16) + x;
		}
	}
	if (length == 1)
		*val = (uint16_t)res;
	else if (length == 2) 
		memcpy(val, &res, 4);
	else 
		memcpy(val, data, length * 2);
}

ModbusValueWord::ModbusValueWord(unsigned int len) : ModbusValue(len), val(0) {
	val = new uint16_t[len];
}

ModbusValueWord::ModbusValueWord(unsigned int len, const std::string &form) : ModbusValue(len, form), val(0) {
	val = new uint16_t[len]; 
}


std::ostream &ModbusMonitor::operator<<(std::ostream &out) const {
	return out << name_ << " " << group_  << ":"
		<< std::setw(4) << std::setfill('0') << address_ << " " << len_ 
		<< " value-length: " << value->length;
}

std::ostream &operator<<(std::ostream &out, const ModbusMonitor &m) {
	return m.operator<<(out);
}


ModbusMonitor::ModbusMonitor(std::string name, unsigned int group, unsigned int address, unsigned int len, const std::string &format)
: name_(name), group_(group),address_(address), len_(len), value(0)
{
	if (group_==0 || group_==1)
		value = new ModbusValueBit(len_);
	else if (group_==3 || group_==4)
		value = new ModbusValueWord(len_, format);
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
		//std::cout << "no device for address " << adr << "\n";
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
			std::string format;
			if (group == 1 || group == 0) format = "BIT";
			else if (group == 3 || group == 4) format = "SignedInt";
			else format = "WORD";
			char *rest = 0;
			address = strtol(code.c_str()+2, &rest, 10);
			ModbusMonitor *m = new ModbusMonitor(name, group, address, len, format);
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


void MonitorConfiguration::createSimulator(const char *filename) {
	std::ofstream simfile(filename);
	if (simfile.good()) {
		std::map<std::string, ModbusMonitor>::iterator iter = monitors.begin();
		while (iter != monitors.end()) {
			const ModbusMonitor &mm = (*iter++).second;
			simfile << mm.name();
			if (mm.group() == 1) simfile << " FLAG(export:ro);";
			else if (mm.group() == 0) simfile << " FLAG(export:rw); ";
			else if (mm.group() == 3)  {
				if (mm.length() == 1)
					simfile << " VARIABLE(export:reg) 0;";
				else if (mm.length() == 2)
					simfile << " VARIABLE(export:reg32) 0;";
				else
					simfile << " VARIABLE(export:str, strlen:" << mm.length() << ") 0;";
			}
			else if (mm.group() == 4) {
				if (mm.length() == 1)
					simfile << " VARIABLE(export:rw_reg) 0;";
				else if (mm.length() == 2)
					simfile << " VARIABLE(export:rw_reg32) 0;";
				else
					simfile << " VARIABLE(export:rw_str, strlen:" << mm.length() << ") 0;";
			}
			simfile << "\n";
		}
	}
	std::string modbus_fname(filename);
	modbus_fname += ".modbus";
	std::ofstream mbfile(modbus_fname);
	if (mbfile.good()) {
		std::map<std::string, ModbusMonitor>::iterator iter = monitors.begin();
		while (iter != monitors.end()) {
			const ModbusMonitor &mm = (*iter++).second;
			mbfile << mm.group() << ":" << mm.address()+1 << "\t" << mm.name() << "\t";
			if (mm.group() == 1 || mm.group() == 0) mbfile << "Discrete\t1\n";
			else if (mm.group() == 3 || mm.group() == 4) {
				if (mm.length() == 1) mbfile << "Signed_int_16";
				else if (mm.length() == 2) mbfile << "Signed_int_32";
				else mbfile << "Ascii_String";
				mbfile << "\t" << mm.length() << "\n";
			}
		}
	}
}



