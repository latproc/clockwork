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

#include "DebugExtra.h"
#include "Logger.h"

DebugExtra *DebugExtra::instance_ = 0;
DebugExtra::DebugExtra() {
    auto logstate = LogState::instance();
    DEBUG_PARSER = logstate->define("DEBUG_PARSER");
    DEBUG_PREDICATES = logstate->define("DEBUG_PREDICATES");
    DEBUG_MESSAGING = logstate->define("DEBUG_MESSAGING");
    DEBUG_STATECHANGES = logstate->define("DEBUG_STATECHANGES");
    DEBUG_SCHEDULER = logstate->define("DEBUG_SCHEDULER");
    DEBUG_AUTOSTATES = logstate->define("DEBUG_AUTOSTATES");
    DEBUG_MACHINELOOKUPS = logstate->define("DEBUG_MACHINELOOKUPS");
    DEBUG_PROPERTIES = logstate->define("DEBUG_PROPERTIES");
    DEBUG_DEPENDANCIES = logstate->define("DEBUG_DEPENDANCIES");
    DEBUG_ACTIONS = logstate->define("DEBUG_ACTIONS");
    DEBUG_INITIALISATION = logstate->define("DEBUG_INITIALISATION");
    DEBUG_MODBUS = logstate->define("DEBUG_MODBUS");
    DEBUG_DISPATCHER = logstate->define("DEBUG_DISPATCHER");
    DEBUG_CHANNELS = logstate->define("DEBUG_CHANNELS");
    DEBUG_ETHERCAT = logstate->define("DEBUG_ETHERCAT");
    DEBUG_PROCESSING = logstate->define("DEBUG_PROCESSING");
    DEBUG_ETHERCAT_CALLS = logstate->define("DEBUG_ETHERCAT_CALLS");
    DEBUG_ETHERCAT_SDO = logstate->define("DEBUG_ETHERCAT_SDO");
    DEBUG_ETHERCAT_PACKETS = logstate->define("DEBUG_ETHERCAT_PACKETS");
}

DebugExtra *DebugExtra::instance() {
    if (!instance_) {
        instance_ = new DebugExtra;
    }
    return instance_;
}
