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

#define EC_SIMULATOR
#include <unistd.h>
#include "ECInterface.h"
#include "ControlSystemMachine.h"
#include "IOComponent.h"
#include <sstream>
#include <stdio.h>
#include <zmq.hpp>

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <list>
#include <map>
#include <utility>
#include <fstream>
#include <sys/stat.h>
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include <signal.h>

#include "cJSON.h"

#define __MAIN__
#include "cwlang.h"
#include "options.h"
#include <stdio.h>
#include "Logger.h"
#include "IODCommand.h"
#include "Dispatcher.h"
#include "Statistic.h"
#include "symboltable.h"
#include "DebugExtra.h"
#include "Scheduler.h"
#include "PredicateAction.h"
#include "ModbusInterface.h"
#include "IODCommands.h"
#include "Statistics.h"
#include "clockwork.h"
#include "ClientInterface.h"
#include "MQTTInterface.h"
#include "MessageLog.h"

bool program_done = false;
bool machine_is_ready = false;

void usage(int argc, char *argv[]);
void displaySymbolTable();

Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;

typedef std::map<std::string, IOComponent*> DeviceList;
DeviceList devices;

typedef std::list<std::string> StringList;
std::map<std::string, StringList> wiring;

boost::mutex thread_protection_mutex;
static boost::mutex io_mutex;
static boost::mutex model_mutex;
boost::condition_variable_any io_updated;
boost::condition_variable_any model_updated;
boost::condition_variable_any ecat_polltime;

