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
#include <map>
#include <zmq.hpp>
#include <boost/thread/mutex.hpp>

#include "IODCommand.h"
#include "ClientInterface.h"
#include "IODCommands.h"
#include "Logger.h"
#include "value.h"
#include "MessagingInterface.h"
#include "options.h"
#include "Dispatcher.h"
#include "MessageEncoding.h"
#include <pthread.h>
#include "MessageLog.h"

extern bool machine_is_ready;
IODCommandThread *IODCommandThread::instance_;

IODCommandThread *IODCommandThread::instance() {
	if (!instance_) instance_ = new IODCommandThread;
	return instance_;
}


struct ListenerThreadInternals : public ClientInterfaceInternals {
    
};

void IODCommandListenerThread::operator()() {
#ifdef __APPLE__
    pthread_setname_np("iod command listener");
#else
    pthread_setname_np(pthread_self(), "iod command listener");
#endif
}
IODCommandListenerThread::IODCommandListenerThread() : done(false){ }
void IODCommandListenerThread::stop() { done = true; }


struct CommandThreadInternals : public ClientInterfaceInternals {
public:
    zmq::socket_t socket;
    pthread_t monitor_thread;
	std::multimap<std::string, IODCommand*> commands;
	boost::mutex data_mutex;

    CommandThreadInternals() : socket(*MessagingInterface::getContext(), ZMQ_REP) {}
};

void IODCommandThread::registerCommand(std::string name, IODCommand *cmd) {

}


class MyMonitor : public zmq::monitor_t {
public:
    MyMonitor(zmq::socket_t *s) : sock(s) {
    }
    void operator()() {
#ifdef __APPLE__
        pthread_setname_np("iod connection monitor");
#else
        pthread_setname_np(pthread_self(), "iod connection monitor");
#endif
        monitor(*sock, "inproc://monitor.rep");
    }
    virtual void on_monitor_started() {
        //std::cerr << "command channel monitor started\n";
    }
    virtual void on_event_connected(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "command channel on_event_connected " << addr_ << "\n";
    }
    virtual void on_event_connect_delayed(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "command channel on_event_connect_delayed " << addr_ << "\n";
    }
    virtual void on_event_connect_retried(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "command channel command channel on_event_connect_retried " << addr_ << "\n";
    }
    virtual void on_event_listening(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "command channel on_event_listening " << addr_ << "\n";
    }
    virtual void on_event_bind_failed(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "command channel on_event_bind_failed " << addr_ << "\n";
    }
    virtual void on_event_accepted(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "command channel on_event_accepted " << event_.value << " " << addr_ << "\n";
    }
    virtual void on_event_accept_failed(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "command channel on_event_accept_failed " << addr_ << "\n";
    }
    virtual void on_event_closed(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "command channel on_event_closed " << addr_ << "\n";
    }
    virtual void on_event_close_failed(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "command channel on_event_close_failed " << addr_ << "\n";
    }
    virtual void on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "command channel on_event_disconnected "<< event_.value << " "  << addr_ << "\n";
    }
    virtual void on_event_unknown(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "command channel on_event_unknown " << addr_ << "\n";
    }

private:
    zmq::socket_t *sock;
};

void IODCommandThread::newPendingCommand(IODCommand *cmd) {
	CommandThreadInternals *cti = dynamic_cast<CommandThreadInternals*>(internals);
	boost::mutex::scoped_lock(cti->data_mutex);

	pending_commands.push_back(cmd);
}

IODCommand *IODCommandThread::getCommand() {
	CommandThreadInternals *cti = dynamic_cast<CommandThreadInternals*>(internals);
	boost::mutex::scoped_lock(cti->data_mutex);

	if (pending_commands.empty()) return 0;
	
	IODCommand *cmd = pending_commands.front();
	pending_commands.pop_front();
	return cmd;
}

IODCommand *IODCommandThread::getCompletedCommand() {
	CommandThreadInternals *cti = dynamic_cast<CommandThreadInternals*>(internals);
	boost::mutex::scoped_lock(cti->data_mutex);

	if (completed_commands.empty()) return 0;

	IODCommand *cmd = completed_commands.front();
	completed_commands.pop_front();
	return cmd;
}

void IODCommandThread::putCompletedCommand(IODCommand *cmd) {
	CommandThreadInternals *cti = dynamic_cast<CommandThreadInternals*>(internals);
	boost::mutex::scoped_lock(cti->data_mutex);

	completed_commands.push_back(cmd);
}

void IODCommand::setParameters(std::vector<Value> &params) {
	std::copy(params.begin(), params.end(), back_inserter(parameters));
}

IODCommand *parseCommandString(const char *data) {
	std::list<Value> parts;
	size_t count = 0;
	std::string ds;
	std::vector<Value> params;
	std::list<Value> *param_list = 0;
	if (MessageEncoding::getCommand(data, ds, &param_list)) {
		params.push_back(ds);
		if (param_list) {
			std::list<Value>::const_iterator iter = param_list->begin();
			while (iter != param_list->end()) {
				const Value &v  = *iter++;
				params.push_back(v);
			}
			param_list->clear();
			delete param_list;
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

	if (params.empty()) {
		return 0;
	}
	IODCommand *command = 0;
	ds = params[0].asString();
	if (count == 1 && ds == "LIST") {
		command = new IODCommandList;
	}
	else if (ds == "GET" && count==2) {
		command = new IODCommandGetStatus;
	}
	else if (ds == "GET" && count==3) {
		command = new IODCommandGetProperty;
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
	else if (count == 3 && ds == "STATE") {
		command =  new IODCommandSetStatus;
	}
	else if (count == 2 && ds == "DEBUG" && params[1] == "SHOW") {
		command = new IODCommandDebugShow;
	}
	else if (count == 3 && ds == "DEBUG") {
		command = new IODCommandDebug;
	}
	else if (count == 2 && ds == "TRACING") {
		command = new IODCommandTracing;
	}
	else if (count == 2 && ds == "TOGGLE") {
		command = new IODCommandToggle;
	}
	else if ( ds == "SEND") {
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
	else if (count >= 2 && ds == "CHANNEL") {
		command = new IODCommandChannel;
	}
	else if (ds == "CHANNELS") {
		command = new IODCommandChannels();
	}
	else if (count > 2 && ds == "DATA") {
		command = new IODCommandData;
	}
	else if (ds == "PROPERTY") {
		command = new IODCommandProperty(data);
	}
	else if (ds == "PERSISTENT") {
		command = new IODCommandPersistentState;
	}
	else if (ds == "HELP") {
		command = new IODCommandHelp;
	}
	else if (ds == "MESSAGES" || (ds == "CLEAR" && count == 2 && params[1] == "MESSAGES") ) {
		command = new IODCommandShowMessages;
	}
	else if (ds == "SCHEDULER") {
		command = new IODCommandSchedulerState;
	}
	else if (count == 2 && ds == "FIND") {
		command = new IODCommandFind;
	}
	else if (ds == "NOTICE") {
		command = new IODCommandNotice;
	}
	else if (ds == "INFO") {
		command = new IODCommandInfo;
	}
	else {
		command = new IODCommandUnknown;
	}
	command->setParameters(params);
	return command;
}


void IODCommandThread::operator()() {
#ifdef __APPLE__
    pthread_setname_np("iod command interface");
#else
    pthread_setname_np(pthread_self(), "iod command interface");
#endif

    CommandThreadInternals *cti = dynamic_cast<CommandThreadInternals*>(internals);

    NB_MSG << "------------------ Command Thread Started -----------------\n";

    MyMonitor monit(&cti->socket);
    boost::thread cmd_monitor(boost::ref(monit));
    
    int linger = 0; // do not wait at socket close time
    cti->socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
	char url_buf[30];
	snprintf(url_buf, 30, "tcp://*:%d", command_port());
    cti->socket.bind (url_buf);
    
    zmq::socket_t access_req(*MessagingInterface::getContext(), ZMQ_REQ);
    access_req.connect("inproc://resource_mgr");
    

    enum {e_running, e_wait_processing_start, e_wait_processing, e_responding} status = e_running; //are we holding shared resources?
    while (!done) {
        try {
            if (status == e_wait_processing_start) {
				//NB_MSG << "e_wait_processing_start\n";
				access_req.send("access", 6);
				// wait while processing occurs
				status = e_wait_processing;
				//NB_MSG << "Client Interface e_wait_processing_start->e_wait_processing\n";
			}
			if (status == e_wait_processing) {
/*
 processing thread will call: (*command)(params) for all pending commands
 */
				size_t acc_len = 0;
				char acc_buf[10];
				if (safeRecv(access_req, acc_buf, 10, true, acc_len, -1)) {
                	status = e_responding;
					//NB_MSG << "Client Interface e_wait_processing->e_responding\n";
				}
            }
            if (status == e_responding) {
				// let the processing thread continue straight away
				access_req.send("done", 4);
				// wait for an acknowledgement so the access request is back in the correct state
				{
					size_t acc_len = 0;
					char acc_buf[10];
					safeRecv(access_req, acc_buf, 10, true, acc_len);
				}

				CommandThreadInternals *cti = dynamic_cast<CommandThreadInternals*>(internals);
				boost::mutex::scoped_lock(cti->data_mutex);
				IODCommand *command = getCompletedCommand();
				while (command) {
					assert(command->done != IODCommand::Unassigned);
					if (command->done == IODCommand::Success) {
						const char * cmdres = command->result();
						//NB_MSG << "Client Interface command generated response: " << cmdres << "\n";
						if (!(*cmdres)) {
							char buf[100];
							snprintf(buf, 100, "command generated an empty response");
							MessageLog::instance()->add(buf);
							NB_MSG << buf << "\n";
							cmdres = "NULL";
						}
						//NB_MSG << "Client interface sending response: " << cmdres << "\n";
						sendMessage(cti->socket, cmdres);
					}
					else {
						NB_MSG << "Client interface saw command error: " << command->error() << "\n";
						sendMessage(cti->socket, command->error());
					}
					delete command;
					command = getCompletedCommand();
				}
				status = e_running;
				//NB_MSG << "Client Interface e_responding->e_running\n";
            }
			if (status == e_running) {
                zmq::pollitem_t items[] = { { cti->socket, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 } };
                int rc = zmq::poll( &items[0], 1, 500);
                if (!rc) continue;
                if (done) break;
                if ( !(items[0].revents & ZMQ_POLLIN) ) continue;

                zmq::message_t request;
                if (!cti->socket.recv (&request)) continue; // interrupted system call
                if (!machine_is_ready) {
                    sendMessage(cti->socket, "Ignored during startup");
                    continue;
                }
                size_t size = request.size();
                char *data = (char *)malloc(size+1); // note: leaks if an exception is thrown
                memcpy(data, request.data(), size);
                data[size] = 0;
//NB_MSG << "Client interface received:" << data << "\n";
				IODCommand *command = parseCommandString(data);
				if (command == 0) {
					sendMessage(cti->socket, "Empty message received\n");
					goto cleanup;
				}
                status = e_wait_processing_start;
				//NB_MSG << "Client Interface e_running->e_wait_processing_start\n";
				newPendingCommand(command);

            cleanup:
                free(data);
            }
        }
        catch (std::exception e) {
            // when processing is stopped, we expect to get an exception on this interface
            // otherwise, we fail since something strange has happened
            if (!done) {
                if (errno) NB_MSG << "error during client communication: " << strerror(errno) << "\n" << std::flush;
                if (zmq_errno())
                    std::cerr << "zmq message: " << zmq_strerror(zmq_errno()) << "\n" << std::flush;
                else
                    std::cerr << " Exception: " << e.what() << "\n" << std::flush;
                if (zmq_errno() != EINTR && zmq_errno() != EAGAIN) abort();
            }
        }
    }
    monit.abort();
    cti->socket.close();
}
ClientInterfaceInternals::~ClientInterfaceInternals() {}

IODCommandThread::IODCommandThread() : done(false), internals(0) {
    internals = new CommandThreadInternals;
}

IODCommandThread::~IODCommandThread() {
	delete internals;
}

void IODCommandThread::stop() {
    CommandThreadInternals *cti = dynamic_cast<CommandThreadInternals*>(internals);
    if (cti->socket) cti->socket.close();
}

