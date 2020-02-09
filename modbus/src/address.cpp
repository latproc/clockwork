#include <iostream>
#include <string>
#include <stdlib.h>
#include "plc_interface.h"

using namespace std;

int main(int argc, char *argv[]) {
	PLCInterface plc;
	if (!plc.load("modbus_addressing.conf")) {
		cerr << "Failed to load configuration\n";
		exit(1);
	}
	int i;
	for (i=1;i<argc;++i) plc.decode(argv[i]);
}
