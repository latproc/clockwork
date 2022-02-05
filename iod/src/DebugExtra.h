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

#ifndef __DebugExtra_H__
#define __DebugExtra_H__

#include "Logger.h"

class DebugExtra {
  public:
    static DebugExtra *instance() {
        if (!instance_) {
            instance_ = new DebugExtra;
        }
        return instance_;
    }
    int DEBUG_PARSER;
    int DEBUG_PREDICATES;
    int DEBUG_MESSAGING;
    int DEBUG_STATECHANGES;
    int DEBUG_SCHEDULER;
    int DEBUG_AUTOSTATES;
    int DEBUG_MACHINELOOKUPS;
    int DEBUG_PROPERTIES;
    int DEBUG_DEPENDANCIES;
    int DEBUG_ACTIONS;
    int DEBUG_INITIALISATION;
    int DEBUG_MODBUS;
    int DEBUG_DISPATCHER;
    int DEBUG_CHANNELS;
    int DEBUG_ETHERCAT;
    int DEBUG_PROCESSING;

  private:
    DebugExtra();
    static DebugExtra *instance_;
};

#define DBG_PARSER MSG(DebugExtra::instance()->DEBUG_PARSER)
#define DBG_PREDICATES MSG(DebugExtra::instance()->DEBUG_PREDICATES)
#define DBG_MESSAGING MSG(DebugExtra::instance()->DEBUG_MESSAGING)
#define DBG_STATECHANGES MSG(DebugExtra::instance()->DEBUG_STATECHANGES)
#define DBG_SCHEDULER MSG(DebugExtra::instance()->DEBUG_SCHEDULER)
#define DBG_AUTOSTATES MSG(DebugExtra::instance()->DEBUG_AUTOSTATES)
#define DBG_MACHINELOOKUPS MSG(DebugExtra::instance()->DEBUG_MACHINELOOKUPS)
#define DBG_PROPERTIES MSG(DebugExtra::instance()->DEBUG_PROPERTIES)
#define DBG_DEPENDANCIES MSG(DebugExtra::instance()->DEBUG_DEPENDANCIES)
#define DBG_ACTIONS MSG(DebugExtra::instance()->DEBUG_ACTIONS)
#define DBG_INITIALISATION MSG(DebugExtra::instance()->DEBUG_INITIALISATION)
#define DBG_MODBUS MSG(DebugExtra::instance()->DEBUG_MODBUS)
#define DBG_DISPATCHER MSG(DebugExtra::instance()->DEBUG_DISPATCHER)
#define DBG_CHANNELS MSG(DebugExtra::instance()->DEBUG_CHANNELS)
#define DBG_ETHERCAT MSG(DebugExtra::instance()->DEBUG_ETHERCAT)
#define DBG_PROCESSING MSG(DebugExtra::instance()->DEBUG_PROCESSING)

#define DBG_M_PARSER M_MSG(DebugExtra::instance()->DEBUG_PARSER, this)
#define DBG_M_PREDICATES M_MSG(DebugExtra::instance()->DEBUG_PREDICATES, this)
#define DBG_M_MESSAGING M_MSG(DebugExtra::instance()->DEBUG_MESSAGING, this)
#define DBG_M_STATECHANGES M_MSG(DebugExtra::instance()->DEBUG_STATECHANGES, this)
#define DBG_M_SCHEDULER M_MSG(DebugExtra::instance()->DEBUG_SCHEDULER, this)
#define DBG_M_AUTOSTATES M_MSG(DebugExtra::instance()->DEBUG_AUTOSTATES, this)
#define DBG_M_MACHINELOOKUPS M_MSG(DebugExtra::instance()->DEBUG_MACHINELOOKUPS, this)
#define DBG_M_PROPERTIES M_MSG(DebugExtra::instance()->DEBUG_PROPERTIES, this)
#define DBG_M_DEPENDANCIES M_MSG(DebugExtra::instance()->DEBUG_DEPENDANCIES, this)
#define DBG_M_ACTIONS M_MSG(DebugExtra::instance()->DEBUG_ACTIONS, this)
#define DBG_M_INITIALISATION M_MSG(DebugExtra::instance()->DEBUG_INITIALISATION, this)
#define DBG_M_MODBUS M_MSG(DebugExtra::instance()->DEBUG_MODBUS, this)

#endif
