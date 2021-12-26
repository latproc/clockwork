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

#ifndef __ETHERCAT_SETUP_H_
#define __ETHERCAT_SETUP_H_

#include <map>

/*  the configuration selector is used to nominate which modules are to be
    configured from xml files manually rather than from a bus
    scan.
*/
class ManualConfigurationSelector {
    unsigned int bus_position;
    std::string xml_file_name;
    uint32_t product_code;         // optional
    uint32_t release_no;           // optional
    std::string alternate_sm_name; // leave blank if not required
};

void initialiseOutputs();

class DeviceInfo;
void generateIOComponentModules(
    std::map<unsigned int, DeviceInfo *> slave_configuration);

#endif
