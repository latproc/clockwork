#include <iostream>
#include <iomanip>
#include <fstream>
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


unsigned int fromOctal(const char *s) {
	unsigned int x;
	char *p = 0;
	x = strtol(s, &p, 8);
	if (errno != 0) std::cerr << "strtol conversion error: " << strerror(errno) << "\n"; 
	if (*p) std::cerr << "warning data left over after number conversion: " << p << "\n";
	return x;
}
