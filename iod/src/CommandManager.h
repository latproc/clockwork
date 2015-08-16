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

#ifndef cwlang_CommandManager_h
#define cwlang_CommandManager_h

#include <string>
#include <zmq.hpp>
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "ConnectionManager.h"

// CommandManager maintain a connection to the command channel of clockwork
class CommandManager : public ConnectionManager {
public:
    enum Status{e_waiting_cmd, e_waiting_response, e_startup, e_disconnected, e_waiting_connect, e_done };
    CommandManager(const char *host, int port);
    void init();
    bool setupConnections();
    bool checkConnections();
    bool checkConnections(zmq::pollitem_t *items, int num_items, zmq::socket_t &cmd);
    virtual int numSocks() { return 1; }

	Status setupStatus() const { return setup_status; } // always e_not_used for non client instances
	void setSetupStatus( Status new_status );
	
	std::string host_name;
	int port;
    zmq::socket_t *setup;
    SingleConnectionMonitor *monit_setup;
    Status setup_status;
    Status run_status;
	uint64_t state_start;
};

#endif
