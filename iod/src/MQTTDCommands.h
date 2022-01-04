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

#ifndef __MQTTDCOMMANDS_H
#define __MQTTDCOMMANDS_H 1

#include "MQTTDCommand.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <zmq.hpp>

void sendMessage(zmq::socket_t &socket, const char *message);

extern std::map<std::string, std::string> message_handlers;

struct MQTTDCommandGetStatus : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandSetStatus : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandResume : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandProperty : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandDescribe : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandList : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandListJSON : public MQTTDCommand {
    static std::set<std::string> no_display;
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandQuit : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandHelp : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandDebugShow : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandDebug : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

struct MQTTDCommandUnknown : public MQTTDCommand {
    bool run(std::vector<std::string> &params);
};

#endif
