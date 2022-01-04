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

#include "Configuration.h"
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdlib.h>
#include <string>

Configuration::Configuration(const std::string filename) {
    std::ifstream cf(filename.c_str());
    std::string line;
    while (getline(cf, line)) {
        if (line[0] != '#') {
            std::istringstream ss(line);
            std::string key, value;
            ss >> key >> value;
            if (key.length() != 0 && value.length() != 0) {
                settings[key] = value;
            }
        }
    }
}

const std::string &Configuration::setting(std::string name) const {
    ConfigurationMap::const_iterator found = settings.find(name);
    if (found == settings.end()) {
        return missing;
    }
    return (*found).second;
}

int Configuration::asInt(std::string name) const {
    std::string val = setting(name);
    if (val.length() == 0) {
        return 0;
    }
    return strtol(val.c_str(), NULL, 0);
}

#ifdef USAGE_EXAMPLE
int main(int argc, char *argv[]) {
    if (argc == 2) {
        Configuration c(argv[1]);
        std::cout << "Setting Test has the value: " << c.setting("Test")
                  << " as integer: " << c.asInt("Test") << "\n";
    }
    return EXIT_SUCCESS;
}
#endif
