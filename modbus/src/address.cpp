#include "plc_interface.h"
#include <iostream>
#include <stdlib.h>
#include <string>

using namespace std;

int main(int argc, char *argv[]) {
    PLCInterface plc;
    if (!plc.load("modbus_addressing.conf")) {
        cerr << "Failed to load configuration\n";
        exit(1);
    }
    int i;
    for (i = 1; i < argc; ++i)
        plc.decode(argv[i]);
}
