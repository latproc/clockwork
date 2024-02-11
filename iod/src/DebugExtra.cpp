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
    DEBUG_PARSER = LogState::instance()->define("DEBUG_PARSER");
    DEBUG_PREDICATES = LogState::instance()->define("DEBUG_PREDICATES");
    DEBUG_MESSAGING = LogState::instance()->define("DEBUG_MESSAGING");
    DEBUG_STATECHANGES = LogState::instance()->define("DEBUG_STATECHANGES");
    DEBUG_SCHEDULER = LogState::instance()->define("DEBUG_SCHEDULER");
    DEBUG_AUTOSTATES = LogState::instance()->define("DEBUG_AUTOSTATES");
    DEBUG_MACHINELOOKUPS = LogState::instance()->define("DEBUG_MACHINELOOKUPS");
    DEBUG_PROPERTIES = LogState::instance()->define("DEBUG_PROPERTIES");
    DEBUG_DEPENDANCIES = LogState::instance()->define("DEBUG_DEPENDANCIES");
    DEBUG_ACTIONS = LogState::instance()->define("DEBUG_ACTIONS");
    DEBUG_INITIALISATION = LogState::instance()->define("DEBUG_INITIALISATION");
    DEBUG_MODBUS = LogState::instance()->define("DEBUG_MODBUS");
    DEBUG_DISPATCHER = LogState::instance()->define("DEBUG_DISPATCHER");
    DEBUG_CHANNELS = LogState::instance()->define("DEBUG_CHANNELS");
    DEBUG_ETHERCAT = LogState::instance()->define("DEBUG_ETHERCAT");
    DEBUG_PROCESSING = LogState::instance()->define("DEBUG_PROCESSING");
    DEBUG_ETHERCAT_CALLS = LogState::instance()->define("DEBUG_ETHERCAT_CALLS");
    DEBUG_ETHERCAT_SDO = LogState::instance()->define("DEBUG_ETHERCAT_SDO");
    DEBUG_ETHERCAT_PACKETS = LogState::instance()->define("DEBUG_ETHERCAT_PACKETS");
}
