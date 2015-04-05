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

extern bool machine_is_ready;

struct ListenerThreadInternals : public ClientInterfaceInternals {
    
};

void IODCommandListenerThread::operator()() {
#ifdef __APPLE__
    pthread_setname_np("iod command listener");
#else
    pthread_setname_np(pthread_self(), "iod command listener");
#endif
}
IODCommandListenerThread::IODCommandListenerThread() : done(false), internals(0) { }
void IODCommandListenerThread::stop() { done = true; }


struct CommandThreadInternals : public ClientInterfaceInternals {
public:
    zmq::socket_t socket;
    pthread_t monitor_thread;
	std::multimap<std::string, IODCommand*> commands;
    
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
        std::cerr << "monitor started\n";
    }
    virtual void on_event_connected(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "on_event_connected " << addr_ << "\n";
    }
    virtual void on_event_connect_delayed(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_connect_delayed " << addr_ << "\n";
    }
    virtual void on_event_connect_retried(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_connect_retried " << addr_ << "\n";
    }
    virtual void on_event_listening(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_listening " << addr_ << "\n";
    }
    virtual void on_event_bind_failed(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_bind_failed " << addr_ << "\n";
    }
    virtual void on_event_accepted(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "on_event_accepted " << event_.value << " " << addr_ << "\n";
    }
    virtual void on_event_accept_failed(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_accept_failed " << addr_ << "\n";
    }
    virtual void on_event_closed(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_closed " << addr_ << "\n";
    }
    virtual void on_event_close_failed(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_close_failed " << addr_ << "\n";
    }
    virtual void on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
        //std::cerr << "on_event_disconnected "<< event_.value << " "  << addr_ << "\n";
    }
    virtual void on_event_unknown(const zmq_event_t &event_, const char* addr_) {
        std::cerr << "on_event_unknown " << addr_ << "\n";
    }

private:
    zmq::socket_t *sock;
};

void IODCommandThread::operator()() {
#ifdef __APPLE__
    pthread_setname_np("iod command interface");
#else
    pthread_setname_np(pthread_self(), "iod command interface");
#endif

    CommandThreadInternals *cti = dynamic_cast<CommandThreadInternals*>(internals);
    
    std::cout << "------------------ Command Thread Started -----------------\n";
    
    MyMonitor monit(&cti->socket);
    boost::thread cmd_monitor(boost::ref(monit));
    
    int linger = 0; // do not wait at socket close time
    cti->socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
	char url_buf[30];
	snprintf(url_buf, 30, "tcp://0.0.0.0:%d", command_port());
    cti->socket.bind (url_buf);
    
    zmq::socket_t access_req(*MessagingInterface::getContext(), ZMQ_REQ);
    access_req.connect("inproc://resource_mgr");
    
    IODCommand *command = 0;
    std::vector<Value> params(0);
    
    enum {e_running, e_waiting_access, e_holding_access} status = e_running; //are we holding shared resources?
    while (!done) {
        try {
            if (status == e_waiting_access) {
				access_req.send("access", 6);
				size_t acc_len = 0;
				char acc_buf[10];
				if (safeRecv(access_req, acc_buf, 10, true, acc_len, -1))
                	status = e_holding_access;
            }
            if (status == e_holding_access) {
                if ((*command)(params)) {
                    const char * cmdres = command->result();
                    //std::cout << " command generated response: " << cmdres << "\n";
                    sendMessage(cti->socket, cmdres);
                }
                else {
                    NB_MSG << command->error() << "\n";
                    sendMessage(cti->socket, command->error());
                }
                delete command;
                params.clear();
                access_req.send("done", 4);
                // wait for an acknowledgement so the access request is back in the correct state
				{
                    size_t acc_len = 0;
                    char acc_buf[10];
                    safeRecv(access_req, acc_buf, 10, true, acc_len);
                }
                status = e_running;
            }
            else if (status == e_running) {
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
//std::cout << data << "\n";
            
                std::list<Value> parts;
                size_t count = 0;
                std::string ds;
                std::list<Value> *param_list = 0;
                if (MessageEncoding::getCommand(data, ds, &param_list)) {
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
            
                if (params.empty()) {
                    sendMessage(cti->socket, "Empty message received\n");
                    goto cleanup;
                }
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
                else if (ds == "NOTICE") {
                    command = new IODCommandNotice;
                }
                else if (ds == "INFO") {
                    command = new IODCommandInfo;
                }
                else {
                    command = new IODCommandUnknown;
                }
                status = e_waiting_access;
            cleanup:
                free(data);
            }
        }
        catch (std::exception e) {
            // when processing is stopped, we expect to get an exception on this interface
            // otherwise, we fail since something strange has happened
            if (!done) {
                if (errno) std::cout << "error during client communication: " << strerror(errno) << "\n" << std::flush;
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

IODCommandThread::IODCommandThread() : done(false) {
    internals = new CommandThreadInternals;
}

void IODCommandThread::stop() {
    CommandThreadInternals *cti = dynamic_cast<CommandThreadInternals*>(internals);
    done = true;
    if (cti->socket) cti->socket.close();
}

