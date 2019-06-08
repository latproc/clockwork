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

#include <assert.h>
#include <iostream>
#include <exception>
#include <math.h>
#include <zmq.hpp>
#include <map>
#include "Logger.h"
#include "DebugExtra.h"
#include "cJSON.h"
#ifdef DYNAMIC_VALUES
#include "dynamic_value.h"
#endif
#include "symboltable.h"
#include "anet.h"
#include "MessageLog.h"
#include "MessageEncoding.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "ConnectionManager.h"
#include "CommandManager.h"

/* A command manager maintains a connection to the clockwork driver on its
   command socket and passes commands arriving from the program's main
 	thread to the remote driver through the command socket.
 */
bool CommandManager::setupConnections() {
    if (setup_status == e_startup) {
        char url[100];
        snprintf(url, 100, "tcp://%s:%d", host_name.c_str(), port);
        setup->connect(url);
        monit_setup->setEndPoint(url);
        setup_status = e_waiting_connect;
        //usleep(5000);
    }
    return false;
}

bool CommandManager::checkConnections() {
    if (monit_setup->disconnected() ) {
        setup_status = e_startup;
        setupConnections();
        //usleep(50000);
        return false;
    }
    if (monit_setup->disconnected())
        return false;
    return true;
}

bool CommandManager::checkConnections(zmq::pollitem_t *items, int num_items, zmq::socket_t &cmd) {
    int rc = 0;
    if (setup_status!= e_waiting_connect && monit_setup->disconnected() ) {
        if (setup_status != e_waiting_connect) {
						{FileLogger fl(program_name); fl.f() << "CommandManagercheckConnections() attempting to setup connection "
        			<< " setup status is " << setup_status << "\n" << std::flush; }
            setup_status = e_startup;
            setupConnections();
            //usleep(50000);
        }
				else
          {FileLogger fl(program_name); fl.f() << "CommandManager has no client or setup connection: setup status is "
            << setup_status << "\n" << std::flush;
          }

        return false;
    }
    if ( monit_setup->disconnected() )
        rc = zmq::poll( &items[1], num_items-1, 0);
    else
        rc = zmq::poll(&items[0], num_items, 0);
    
    char buf[1000];
    size_t msglen = 0;
    if (rc > 0 && run_status == e_waiting_cmd && items[1].revents & ZMQ_POLLIN) {
		//		{FileLogger fl(program_name); fl.f() << "command socket activity when waiting cmd.  receiving...\n" << std::flush; }
        safeRecv(cmd, buf, 1000, false, msglen, 1);
        if ( msglen ) {
            buf[msglen] = 0;
			//{FileLogger fl(program_name); fl.f() << "got cmd: " << buf << " for clockwork\n"<<std::flush; }
            //DBG_MSG << "got cmd: " << buf << "\n";
            if (!monit_setup->disconnected()) setup->send(buf,msglen);
            run_status = e_waiting_response;
        }
				else {FileLogger fl(program_name); fl.f() << "No Message\n" << std::flush; }
    }
    if (run_status == e_waiting_response && monit_setup->disconnected()) {
				{FileLogger fl(program_name); fl.f() << "disconnected, attempting reconnect\n"<<std::flush; }
        const char *msg = "disconnected, attempting reconnect";
        cmd.send(msg, strlen(msg));
        run_status = e_waiting_cmd;
    }
    else if (rc > 0 && run_status == e_waiting_response && items[0].revents & ZMQ_POLLIN) {
		//{FileLogger fl(program_name); fl.f() << "command socket activity.  receiving...\n" << std::flush; }
        if (safeRecv(*setup, buf, 1000, false, msglen, 0)) {
            if (msglen && msglen<1000) {
                cmd.send(buf, msglen);
            }
            else {
                cmd.send("error", 5);
            }
            run_status = e_waiting_cmd;
        }
    }
    return true;
}

CommandManager::CommandManager(const char *host, int portnum)
	: host_name(host), port(portnum), setup(0), monit_setup(0) {
	    setup = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
	    monit_setup = new SingleConnectionMonitor(*setup);
	    init();
}

void CommandManager::init() {
    // client
    boost::thread setup_monitor(boost::ref(*monit_setup));
    
    run_status = e_waiting_cmd;
    setup_status = e_startup;
}

