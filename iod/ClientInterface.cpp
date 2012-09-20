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


extern bool machine_is_ready;
extern boost::mutex thread_protection_mutex;


void IODCommandThread::operator()() {
    std::cout << "------------------ Command Thread Started -----------------\n";
    zmq::context_t context (1);
    zmq::socket_t socket (context, ZMQ_REP);
    socket.bind ("tcp://*:5555");
    IODCommand *command = 0;
    
    while (!done) {
        zmq::message_t request;
        try {
            
            //  Wait for next request from client
            socket.recv (&request);
            if (!machine_is_ready) {
                sendMessage(socket, "Ignored during startup");
                continue;
            }
            size_t size = request.size();
            char *data = (char *)malloc(size+1);
            memcpy(data, request.data(), size);
            data[size] = 0;
            //std::cout << "Command thread received " << data << std::endl;
            std::istringstream iss(data);
            std::list<std::string> parts;
            std::string ds;
            int count = 0;
            while (iss >> ds) {
                parts.push_back(ds);
                ++count;
            }
            
            boost::mutex::scoped_lock lock(thread_protection_mutex);
            std::vector<std::string> params(0);
            std::copy(parts.begin(), parts.end(), std::back_inserter(params));
            if (params.empty()) {
                sendMessage(socket, "Empty message received\n");
                goto cleanup;
            }
            ds = params[0];
            {
                
                if (ds == "GET" && count>1) {
                    command = new IODCommandGetStatus;
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
                else if (count == 2 && ds == "TOGGLE") {
                    command = new IODCommandToggle;
                }
                else if (count == 1 && ds == "LIST") {
                    command = new IODCommandList;
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
                    command = new IODCommandProperty;
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
                else if (ds == "HELP") {
                    command = new IODCommandHelp;
                }
                else {
                    command = new IODCommandUnknown;
                }
            }
            if ((*command)(params)) {
                //std::cout << command->result() << "\n";
                sendMessage(socket, command->result());
            }
            else {
                NB_MSG << command->error() << "\n";
                sendMessage(socket, command->error());
            }
            delete command;
            
        cleanup:
            
            free(data);
        }
        catch (std::exception e) {
            //std::cout << e.what() << "\n";
            usleep(1000);
            //exit(1);
        }
    }
    socket.close();
}

IODCommandThread::IODCommandThread() : done(false) {
        
}

void IODCommandThread::stop() { done = true; }

