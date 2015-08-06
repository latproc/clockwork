#include <iostream>
#include <string>
#include <stdlib.h>
#include "plc_interface.h"

using namespace std;


int main(int argc, char *argv[]) {
	PLCInterface plc;
	if (!plc.load("koyo.conf")) {
		cerr << "Failed to load configuration\n";
		exit(1);
	}
	int i;
	for (i=1;i<argc;++i) {
		try {
			char code[10];
			int idx = 0;
			const char *addr_str = argv[i];
			while (*addr_str) {
				if (isalpha(*addr_str)) {
					if (idx<10) { code[idx] = (idx==9) ? 0 : *addr_str;} 
					++addr_str;
					++idx;
				}
				else break;
			}
			int addr;
			char *rest = 0;
			addr = strtol(addr_str, &rest, 8);
			if (errno == -1) {std::cerr << "argument error: " << argv[i] << " is invalid\n"; }
			else {
				PLCMapping mapping = plc.mappings.at(code);
				mapping.address() += addr;
				std::cout << argv[i] << " -> " << mapping.group() << " " << mapping.address() << "\n";
			}
		}
		catch (std::exception ex) {
			cerr << "No entry: " << argv[i] << "\n";
		}
	}
}