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

void sendMessage(zmq::socket_t &socket, const char *message);

extern std::map<std::string, std::string> message_handlers;

struct IODCommandGetStatus : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandSetStatus : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandEnable : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandResume : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandDisable : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandToggle : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandProperty : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandDescribe : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandList : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandListJSON : public IODCommand {
	static std::set<std::string>no_display;
	bool run(std::vector<std::string> &params);
};

struct IODCommandSend : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandQuit : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandHelp : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandDebugShow : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandDebug : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandModbus : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandModbusExport : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandModbusRefresh : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandUnknown : public IODCommand {
	bool run(std::vector<std::string> &params);
};


#endif
