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

#ifndef __IODCOMMANDS_H
#define __IODCOMMANDS_H 1

#include <vector>
#include <string>
#include <set>
#include "IODCommand.h"
#include <zmq.hpp>
#include <map>
#include "value.h"

//void sendMessage(zmq::socket_t &socket, const char *message);

extern std::map<std::string, std::string> message_handlers;

struct IODCommandChannel : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandChannels : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandInfo : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandGetStatus : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandGetProperty : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandSetStatus : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandEnable : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandResume : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandDisable : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandToggle : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandProperty : public IODCommand {
    IODCommandProperty(const char *raw_message) : raw_message_(raw_message) {}
	bool run(std::vector<Value> &params);
    std::string raw_message_;
};

struct IODCommandDescribe : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandList : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandListJSON : public IODCommand {
	static std::set<std::string>no_display;
	bool run(std::vector<Value> &params);
};

struct IODCommandSend : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandQuit : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandHelp : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandDebugShow : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandDebug : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandModbus : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandTracing : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandModbusExport : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandModbusRefresh : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandPerformance : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandPersistentState : public IODCommand {
    bool run(std::vector<Value> &params);
};

struct IODCommandChannelRefresh : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandSchedulerState : public IODCommand {
    bool run(std::vector<Value> &params);
};

struct IODCommandState : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandUnknown : public IODCommand {
	bool run(std::vector<Value> &params);
};


struct IODCommandData : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandShowMessages : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandNotice : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandBusy : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandFind : public IODCommand {
	bool run(std::vector<Value> &params);
};

struct IODCommandFreeze : public IODCommand {
	bool run(std::vector<Value> &params);
};



#endif
