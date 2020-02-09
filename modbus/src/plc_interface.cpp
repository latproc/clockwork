#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include "plc_interface.h"


std::ostream &PLCMapping::operator<<(std::ostream &out) const {
	return out << code_ <<": " << start_ <<" " << end_ << " " << group_  << ":"
		<< std::setw(4) << std::setfill('0') << address_;
}

std::ostream &operator<<(std::ostream &out, const PLCMapping &m) {
	return m.operator<<(out);
}


PLCMapping::PLCMapping(const char *code, unsigned int start, unsigned int end, unsigned int group, unsigned int address)
: code_(code),start_(start),end_(end),group_(group),address_(address)
{
	
}

PLCMapping::PLCMapping(const PLCMapping &other)
: code_(other.code_),start_(other.start_),end_(other.end_),group_(other.group_),address_(other.address_)
{
	
}

PLCMapping &PLCMapping::operator=(const PLCMapping &other) 
{
	code_ = other.code_; start_ = other.start_; end_ = other.end_; group_ = other.group_; address_ = other.address_;
	return *this;
}



bool PLCInterface::load(const char *fname) {
	std::ifstream in(fname);
	while (!in.eof()) {
		std::string code;
		unsigned int start;
		unsigned int end;
		unsigned int group;
		unsigned int address;
		in >> code >> start >> end >> group >> address;
		if (in.good()) {
			PLCMapping m(code.c_str(), start, end, group, address);
			mappings.insert(make_pair(code, m));
		}
		else if (!in.eof()) return false;
	}
	return true;
}

std::pair<int, int> PLCInterface::decode(const char *address) {
	try {
		char code[10];
		int idx = 0;
		memset(code, 0, 10);
		const char *addr_str = address;
		while (*addr_str) {
			if (isalpha(*addr_str)) {
				if (idx<10) { code[idx] = (idx==9) ? 0 : *addr_str;} 
				++addr_str;
				++idx;
			}
			else {
				break;
			}
		}
		int addr;
		int base = 10;
		size_t addr_len = strlen(addr_str);
		if (addr_len>2 && addr_str[0] == '0' && addr_str[1] == 'x') {
			base = 16;
			addr_str += 2;
		}
		else if (addr_len>1 && addr_str[0] == '0') {
			base = 8;
			addr_str += 2;
		}
		char *rest = 0;
		addr = strtol(addr_str, &rest, base);
		if (errno == -1) {std::cerr << "argument error: " << address << " is invalid\n"; }
		else {
			PLCMapping mapping = mappings.at(code);
			int address_offset = mapping.address() + addr;
			//std::cout << address << " -> " << mapping.group() << " " << address_offset << "\n";
			return std::make_pair(mapping.group(), address_offset);
		}
	}
	catch (std::exception ex) {
		std::cerr << "No entry: " << address << "\n";
	}
	return std::make_pair(-1, -1);
}


unsigned int fromOctal(const char *s) {
	unsigned int x;
	char *p = 0;
	x = strtol(s, &p, 8);
	if (errno != 0) std::cerr << "strtol conversion error: " << strerror(errno) << "\n"; 
	if (*p) std::cerr << "warning data left over after number conversion: " << p << "\n";
	return x;
}
