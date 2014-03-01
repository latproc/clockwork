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

#include <stdlib.h>
#include <iostream>
#include <string>
#include <sstream>
#include <list>
#include <boost/thread/mutex.hpp>
#include <zmq.hpp>

#include "IODCommand.h"
#include "ClientInterface.h"
#include "IODCommands.h"
#include "Logger.h"
#include "value.h"
#include "MessagingInterface.h"
#include "options.h"

extern bool machine_is_ready;
extern boost::mutex thread_protection_mutex;

struct ListenerThreadInternals : public ClientInterfaceInternals {
    
};

void IODCommandListenerThread::operator()() {}
IODCommandListenerThread::IODCommandListenerThread() : done(false), internals(0) { }
void IODCommandListenerThread::stop() { done = true; }


struct CommandThreadInternals : public ClientInterfaceInternals {
    
};


void IODCommandThread::operator()() {
    std::cout << "------------------ Command Thread Started -----------------\n";
    zmq::context_t context (3);
    zmq::socket_t socket (context, ZMQ_REP);
    int linger = 0; // do not wait at socket close time
    socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    std::stringstream sn;
    sn << "tcp://0.0.0.0:" << command_port();
    const char *port = sn.str().c_str();
    socket.bind (port);
    IODCommand *command = 0;
    
    while (!done) {
        try {
            
             zmq::pollitem_t items[] = { { socket, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 } };
             zmq::poll( &items[0], 1, 500*1000);
             if ( !(items[0].revents & ZMQ_POLLIN) ) continue;

        	zmq::message_t request;
            if (!socket.recv (&request)) continue; // interrupted system call
            if (!machine_is_ready) {
                sendMessage(socket, "Ignored during startup");
                continue;
            }
            size_t size = request.size();
            char *data = (char *)malloc(size+1);
            memcpy(data, request.data(), size);
            data[size] = 0;
            
            std::list<Value> parts;
            int count = 0;
            std::string ds;
            std::vector<Value> params(0);
            {
                std::list<Value> *param_list = 0;
                if (MessagingInterface::getCommand(data, ds, &param_list)) {
                    params.push_back(ds);
                    if (param_list) {
                        std::list<Value>::const_iterator iter = param_list->begin();
                        while (iter != param_list->end()) {
                            const Value &v  = *iter++;
                            params.push_back(v);
                        }
                    }
                    count = params.size();
                }
                else {
                    std::istringstream iss(data);
										std::string tmp;
										iss >> ds;
                    parts.push_back(ds.c_str());
										++count;
                    while (iss >> tmp) {
                        parts.push_back(tmp.c_str());
                        ++count;
                    }
                    std::copy(parts.begin(), parts.end(), std::back_inserter(params));
                }
            }
            
            if (params.empty()) {
                sendMessage(socket, "Empty message received\n");
                goto cleanup;
            }
            ds = params[0].asString();
            {
                boost::mutex::scoped_lock lock(thread_protection_mutex);
                
                if (count == 1 && ds == "LIST") {
                    command = new IODCommandList;
                }
                else if (ds == "GET" && count>1) {
                    command = new IODCommandGetStatus;
                }
                else if (ds == "MODBUS" && count == 2 && params[1] == "EXPORT") {
                    command = new IODCommandModbusExport;
                }
                else if (ds == "MODBUS" && count == 2 && params[1] == "REFRESH") {
                    command = new IODCommandModbusRefresh;
                }
                else if (ds == "MODBUS") {
                    command = new IODCommandModbus;
                }
                else if (count == 4 && ds == "SET" && params[2] == "TO") {
                    command =  new IODCommandSetStatus;
                }
                else if (count == 2 && ds == "DEBUG" && params[1] == "SHOW") {
                    command = new IODCommandDebugShow;
                }
                else if (count == 3 && ds == "DEBUG") {
                    command = new IODCommandDebug;
                }
                else if (count == 3 && ds == "TRACING") {
                    command = new IODCommandTracing;
                }
                else if (count == 2 && ds == "TOGGLE") {
                    command = new IODCommandToggle;
                }
                else if (count == 2 && ds == "SEND") {
                    command = new IODCommandSend;
                }
                else if (count == 1 && ds == "QUIT") {
                    command = new IODCommandQuit;
                }
                else if (count >= 2 && ds == "LIST" && params[1] == "JSON") {
                    command = new IODCommandListJSON;
                }
                else if (count == 2 && ds == "ENABLE") {
                    command = new IODCommandEnable;
                }
                else if (count == 2 && ds == "RESUME") {
                    command = new IODCommandResume;
                }
                else if (count == 4 && ds == "RESUME" && params[2] == "AT") {
                    command = new IODCommandResume;
                }
                else if (count == 2 && ds == "DISABLE") {
                    command = new IODCommandDisable;
                }
                else if ( (count == 2 || count == 3) && ds == "DESCRIBE") {
                    command = new IODCommandDescribe;
                }
                else if ( ds == "STATS" ) {
                    command = new IODCommandPerformance;
                }
                else if (count > 2 && ds == "DATA") {
                    command = new IODCommandData;
                }
                else if (count > 1 && ds == "EC") {
                    command = new IODCommandEtherCATTool;
                }
                else if (ds == "SLAVES") {
                    command = new IODCommandGetSlaveConfig;
                }
                else if (count == 1 && ds == "MASTER") {
                    command = new IODCommandMasterInfo;
                }
                else if (ds == "PROPERTY") {
                    command = new IODCommandProperty(data);
                }
                else if (ds == "HELP") {
                    command = new IODCommandHelp;
                }
                else if (ds == "MESSAGES") {
                    command = new IODCommandShowMessages;
                }
                else if (ds == "NOTICE") {
                    command = new IODCommandNotice;
                }
                else if (ds == "INFO") {
                    command = new IODCommandInfo;
                }
                else {
										std::cout << "unknown command " << data << "\n";
                    command = new IODCommandUnknown;
                }
                if ((*command)(params)) {
                    sendMessage(socket, command->result());
                }
                else {
                    NB_MSG << command->error() << "\n";
                    sendMessage(socket, command->error());
                }
            }
            delete command;
            
        cleanup:
            
            free(data);
        }
        catch (std::exception e) {
			if (errno) std::cout << "error during client communication: " << strerror(errno) << "\n" << std::flush;
            if (zmq_errno())
                std::cerr << zmq_strerror(zmq_errno()) << "\n" << std::flush;
            else
                std::cerr << " Exception: " << e.what() << "\n" << std::flush;
			if (zmq_errno() != EINTR && zmq_errno() != EAGAIN) abort();
        }
    }
    socket.close();
}

IODCommandThread::IODCommandThread() : done(false) {
        
}

void IODCommandThread::stop() { done = true; }

