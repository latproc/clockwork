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
#include <Channel.h>

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
#include "watchdog.h"



uint64_t client_watchdog_timer = 0;
//extern bool machine_is_ready;
static Watchdog *wd;

IODCommandThread *IODCommandThread::instance_;

IODCommandThread *IODCommandThread::instance() {
	if (!instance_) instance_ = new IODCommandThread;
	return instance_;
}

class IODCommandFactory {
public:
	virtual ~IODCommandFactory() {}
	virtual IODCommand *create();
};

IODCommand *IODCommandFactory::create() { return new IODCommandUnknown(); }

struct IODCommandChannelFactory: public IODCommandFactory {
	IODCommandChannel *create() { return new IODCommandChannel(); } };
struct IODCommandChannelsFactory: public IODCommandFactory {
	IODCommandChannels *create() { return new IODCommandChannels(); } };
struct IODCommandInfoFactory: public IODCommandFactory {
	IODCommandInfo *create() { return new IODCommandInfo(); } };
struct IODCommandGetStatusFactory: public IODCommandFactory {
	IODCommandGetStatus *create() { return new IODCommandGetStatus(); } };
struct IODCommandGetPropertyFactory: public IODCommandFactory {
	IODCommandGetProperty *create() { return new IODCommandGetProperty(); } };
struct IODCommandSetStatusFactory: public IODCommandFactory {
	IODCommandSetStatus *create() { return new IODCommandSetStatus(); } };
struct IODCommandEnableFactory: public IODCommandFactory {
	IODCommandEnable *create() { return new IODCommandEnable(); } };
struct IODCommandResumeFactory: public IODCommandFactory {
	IODCommandResume *create() { return new IODCommandResume(); } };
struct IODCommandDisableFactory: public IODCommandFactory {
	IODCommandDisable *create() { return new IODCommandDisable(); } };
struct IODCommandToggleFactory: public IODCommandFactory {
	IODCommandToggle *create() { return new IODCommandToggle(); } };
struct IODCommandPropertyFactory: public IODCommandFactory {
	IODCommandProperty *create() { return 0; }
	IODCommandProperty *createPropertyCommand(const char *raw) {
		return new IODCommandProperty(raw);
	}
};
struct IODCommandDescribeFactory: public IODCommandFactory {
	IODCommandDescribe *create() { return new IODCommandDescribe(); } };
struct IODCommandListFactory: public IODCommandFactory {
	IODCommandList *create() { return new IODCommandList(); } };
struct IODCommandListJSONFactory: public IODCommandFactory {
	IODCommandListJSON *create() { return new IODCommandListJSON(); } };
struct IODCommandSendFactory: public IODCommandFactory {
	IODCommandSend *create() { return new IODCommandSend(); } };
struct IODCommandQuitFactory: public IODCommandFactory {
	IODCommandQuit *create() { return new IODCommandQuit(); } };
struct IODCommandHelpFactory: public IODCommandFactory {
	IODCommandHelp *create() { return new IODCommandHelp(); } };
struct IODCommandDebugShowFactory: public IODCommandFactory {
	IODCommandDebugShow *create() { return new IODCommandDebugShow(); } };
struct IODCommandDebugFactory: public IODCommandFactory {
	IODCommandDebug *create() { return new IODCommandDebug(); } };
struct IODCommandModbusFactory: public IODCommandFactory {
	IODCommandModbus *create() { return new IODCommandModbus(); } };
struct IODCommandTracingFactory: public IODCommandFactory {
	IODCommandTracing *create() { return new IODCommandTracing(); } };
struct IODCommandModbusExportFactory: public IODCommandFactory {
	IODCommandModbusExport *create() { return new IODCommandModbusExport(); } };
struct IODCommandModbusRefreshFactory: public IODCommandFactory {
	IODCommandModbusRefresh *create() { return new IODCommandModbusRefresh(); } };
struct IODCommandPerformanceFactory: public IODCommandFactory {
	IODCommandPerformance *create() { return new IODCommandPerformance(); } };
struct IODCommandPersistentStateFactory: public IODCommandFactory {
	IODCommandPersistentState *create() { return new IODCommandPersistentState(); } };
struct IODCommandChannelRefreshFactory: public IODCommandFactory {
	IODCommandChannelRefresh *create() { return new IODCommandChannelRefresh(); } };
struct IODCommandSchedulerStateFactory: public IODCommandFactory {
	IODCommandSchedulerState *create() { return new IODCommandSchedulerState(); } };
struct IODCommandStateFactory: public IODCommandFactory {
	IODCommandState *create() { return new IODCommandState(); } };
struct IODCommandUnknownFactory: public IODCommandFactory {
	IODCommandUnknown *create() { return new IODCommandUnknown(); } };
struct IODCommandDataFactory: public IODCommandFactory {
	IODCommandData *create() { return new IODCommandData(); } };
struct IODCommandShowMessagesFactory: public IODCommandFactory {
	IODCommandShowMessages *create() { return new IODCommandShowMessages(); } };
struct IODCommandNoticeFactory: public IODCommandFactory {
	IODCommandNotice *create() { return new IODCommandNotice(); } };
struct IODCommandFindFactory: public IODCommandFactory {
	IODCommandFind *create() { return new IODCommandFind(); } };
struct IODCommandFreezeFactory: public IODCommandFactory {
	IODCommandFreeze *create() { return new IODCommandFreeze(); } };

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

template<class T> class CommandTable {
public:
	typedef typename std::multimap<std::string, T> Table;
	typedef typename Table::value_type Node;
	typedef std::pair<typename Table::iterator, typename Table::iterator> Range;
	typedef typename Table::iterator Iterator;

	Table table;

	void add(const char *name, T cmd) { table.insert( std::make_pair(name, cmd) ); }
	Range find(const std::string &name) { return table.equal_range(name); }
};



struct CommandThreadInternals : public ClientInterfaceInternals {
public:
    zmq::socket_t socket;
    //pthread_t monitor_thread;
	CommandTable<IODCommandFactory*> commands;
	boost::mutex data_mutex;

	//std::list<IODCommand *>pending_commands;
	//std::list<IODCommand *>completed_commands;


    CommandThreadInternals() : socket(*MessagingInterface::getContext(), ZMQ_REP) {
		commands.add("CHANNEL", new IODCommandChannelFactory());
		commands.add("CHANNEL", new IODCommandChannelRefreshFactory());
		commands.add("CHANNEL", new IODCommandChannelsFactory());
		commands.add("DATA", new IODCommandDataFactory());
		commands.add("DEBUG", new IODCommandDebugFactory());
		commands.add("DEBUG", new IODCommandDebugShowFactory());
		commands.add("DESCRIBE", new IODCommandDescribeFactory());
		commands.add("DISABLE", new IODCommandDisableFactory());
		commands.add("ENABLE", new IODCommandEnableFactory());
		commands.add("FIND", new IODCommandFindFactory());
		commands.add("FREEZE", new IODCommandFreezeFactory());
		commands.add("GET", new IODCommandGetPropertyFactory());
		commands.add("GET", new IODCommandGetStatusFactory());
		commands.add("HELPINFO", new IODCommandHelpFactory());
		commands.add("INFO", new IODCommandInfoFactory());
		commands.add("LIST", new IODCommandListFactory());
		commands.add("LIST", new IODCommandListJSONFactory());
		commands.add("MODBUS", new IODCommandModbusExportFactory());
		commands.add("MODBUS", new IODCommandModbusFactory());
		commands.add("MODBUS", new IODCommandModbusRefreshFactory());
		commands.add("NOTICE", new IODCommandNoticeFactory());
		commands.add("STATS", new IODCommandPerformanceFactory());
		commands.add("PERSISTENT", new IODCommandPersistentStateFactory());
		commands.add("PROPERTY", new IODCommandPropertyFactory());
		commands.add("QUIT", new IODCommandQuitFactory());
		commands.add("RESUME", new IODCommandResumeFactory());
		commands.add("RESUME", new IODCommandResumeFactory());
		commands.add("SCHEDULER", new IODCommandSchedulerStateFactory());
		commands.add("SEND", new IODCommandSendFactory());
		commands.add("SET", new IODCommandSetStatusFactory());
		commands.add("STATE", new IODCommandSetStatusFactory());
		commands.add("MESSAGES", new IODCommandShowMessagesFactory());
		//commands.add("", new IODCommandStateFactory());
		commands.add("TOGGLE", new IODCommandToggleFactory());
		commands.add("TRACING", new IODCommandTracingFactory());
		//commands.add("", new IODCommandUnknownFactory());
	}
};

void IODCommandThread::registerCommand(std::string name, IODCommandFactory *cmd) {
	CommandThreadInternals *cti
		= dynamic_cast<CommandThreadInternals*>(IODCommandThread::instance()->internals);
	cti->commands.add(name.c_str(), cmd);
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
        DBG_MSG << "CI << command channel monitor started\n";
    }
    virtual void on_event_connected(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << "CI command channel on_event_connected " << addr_ << "\n";
    }
    virtual void on_event_connect_delayed(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << "CI command channel on_event_connect_delayed " << addr_ << "\n";
    }
    virtual void on_event_connect_retried(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << "CI command channel command channel on_event_connect_retried " << addr_ << "\n";
    }
    virtual void on_event_listening(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << "CI command channel on_event_listening " << addr_ << "\n";
    }
    virtual void on_event_bind_failed(const zmq_event_t &event_, const char* addr_) {
			DBG_MSG << "CI command channel on_event_bind_failed " << addr_ << "\n";
    }
    virtual void on_event_accepted(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << "CI command channel on_event_accepted " << event_.value << " " << addr_ << "\n";
    }
    virtual void on_event_accept_failed(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << "CI command channel on_event_accept_failed " << addr_ << "\n";
    }
    virtual void on_event_closed(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << "CI command channel on_event_closed " << addr_ << "\n";
    }
    virtual void on_event_close_failed(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << "CI command channel on_event_close_failed " << addr_ << "\n";
    }
    virtual void on_event_disconnected(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << "CI command channel on_event_disconnected "<< event_.value << " "  << addr_ << "\n";
    }
    virtual void on_event_unknown(const zmq_event_t &event_, const char* addr_) {
        DBG_MSG << "CI command channel on_event_unknown " << addr_ << "\n";
    }

private:
    zmq::socket_t *sock;
};


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
    params.push_back(Value(ds, Value::t_string));
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
		parts.push_back(Value(ds.c_str(), Value::t_string));
		++count;
		while (iss >> tmp) {
      parts.push_back(Value(tmp.c_str(), Value::t_string));
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
	else if (ds == "MODBUS" && (count == 2 || count == 3) && params[1] == "EXPORT") {
		command = new IODCommandModbusExport;
	}
	else if (ds == "MODBUS" && count == 2 && params[1] == "REFRESH") {
		command = new IODCommandModbusRefresh;
	}
	else if (ds == "MODBUS") {
		command = new IODCommandModbus;
	}
	else if (count >= 4 && ds == "SET" && params[2] == "TO") {
		command =  new IODCommandSetStatus;
	}
	else if (count >= 3 && ds == "STATE") {
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
		if (params[1] == "ETHERCAT")
			command = new IODCommandToggleEtherCAT;
		else
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
	else if (count == 2 && ds == "SHOW" && params[1] == "BUSY") {
		command = new IODCommandBusy;
	}
	else if (count == 2 && ds == "SHOW" && params[1] == "TRIGGERS") {
		command = new IODCommandTriggers;
	}
	else if (count == 2 && ds == "SHOW" ) {
		command = new IODCommandShow;
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
	else if (ds == "REFRESH" && count == 2) {
		command = new IODCommandChannelRefresh();
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
#ifdef USE_SDO
	else if (ds == "SDO") {
		command = new IODCommandSDO;
	}
#endif //USE_SDO
	else if (ds == "HELP") {
		command = new IODCommandHelp;
	}
	else if (ds == "MESSAGES" || (ds == "CLEAR" && count == 2 && params[1] == "MESSAGES") ) {
		command = new IODCommandShowMessages;
	}
	else if (ds == "SCHEDULER") {
		command = new IODCommandSchedulerState;
	}
	else if ( (count == 1 || count == 2) && ds == "FIND") {
		command = new IODCommandFind;
	}
	else if (ds == "NOTICE") {
		command = new IODCommandNotice;
	}
	else if (ds == "INFO") {
		command = new IODCommandInfo;
	}
	else if (ds == "FREEZE") {
		command = new IODCommandFreeze;
	}
	else if (ds == "SHUTDOWN") {
		command = new IODCommandShutdown;
	}
	else {
		FileLogger fl(program_name);
		fl.f() << "Warning: no command found for " << data << "\n";
		command = new IODCommandUnknown;
	}
	if (command) command->setParameters(params);
	return command;
}


void IODCommandThread::operator()() {
#ifdef __APPLE__
    pthread_setname_np("iod command interface");
#else
    pthread_setname_np(pthread_self(), "iod command interface");
#endif

	wd = new Watchdog("Command Thread Watchdog", 600, false);
    CommandThreadInternals *cti = dynamic_cast<CommandThreadInternals*>(internals);

    NB_MSG << "------------------ Command Thread Started -----------------\n";

	//MyMonitor monit(&cti->socket);
	//boost::thread cmd_monitor(boost::ref(monit));
    
    int linger = 0; // do not wait at socket close time
	cti->socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
	char url_buf[30];
	int retries = 2; // attempt to use the default command port and auto allocate another if necessary
	int port = command_port();
	while (retries>0) {
		snprintf(url_buf, 30, "tcp://*:%d", port);
		try {
			cti->socket.bind (url_buf);
			break;
		}
		catch (zmq::error_t zex) {
			{	NB_MSG << "Error: trying port " << port << ": " << zmq_strerror(zmq_errno()) << "\n";
				FileLogger fl(program_name); usleep(10);
				fl.f() << "Error: trying port " << port << ": " << zmq_strerror(zmq_errno()) << "\n";
				if (!command_port_fixed() && --retries > 0) {
					port = Channel::uniquePort();
					usleep(100); // give time for the new port to become available
					continue;
				}
				exit(1);
			}
		}
	}
	NB_MSG << "Client Interface available on port: " << port << "\n";

    zmq::socket_t access_req(*MessagingInterface::getContext(), ZMQ_PAIR);
    access_req.bind("inproc://resource_mgr");

	zmq::socket_t command_sync(*MessagingInterface::getContext(), ZMQ_PAIR);
	command_sync.bind("inproc://command_sync");

	char start_cli[20];
	do { // wait to start
		size_t len;
		safeRecv(access_req, start_cli, 19, true, len, -1);
		if (len>20) {
			start_cli[19] = 0;
			FileLogger fl(program_name); fl.f() << "client interface startup got unexpected: " << start_cli << "\n";
		}
		if (len<20) start_cli[len] = 0;;
		NB_MSG << "client thread received: "  << start_cli << "\n";
		usleep(100000);
	} while (strcmp(start_cli, "start") != 0);

    enum {e_running, e_wait_processing_start, e_wait_processing, e_responding} status = e_running; //are we holding shared resources?
	int poll_time = 2;
    while (!done) {
		try {
			wd->stop(); // disable the watchdog while we wait for something to do
			zmq::pollitem_t items[] = {
				{ (void*)cti->socket, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 } ,
				{ (void*)access_req, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 },
				{ (void*)command_sync, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 }
			};
			int rc;
			try {
				rc = zmq::poll( &items[0], 3, poll_time);
				if (poll_time < 20) poll_time += 1;
				if (!rc) { usleep(100); continue; }
				if (rc == -1 && errno == EAGAIN) continue;
				if (done) break;
			}
			catch (zmq::error_t zex) {
				{
					FileLogger fl(program_name);
					fl.f() << "Client Interface exception during poll()\n";
				}
				usleep(100);
				continue;
			}
			wd->poll();

			// use a shorter activity poll for a while since something is happening
			(poll_time < 4) ? poll_time = 2 : poll_time -= 2;

			/*
			   processing thread will call: (*command)(params) for all pending commands
			 */

			if ( items[2].revents & ZMQ_POLLIN) {
				char *buf = 0;
				size_t response_len;
				if (safeRecv(command_sync, &buf, &response_len, true, 0)) {
#if 0
					{
						char line[80];
						snprintf(line, 80, "%s", buf);
						FileLogger fl(program_name);
						fl.f() << "client interface received " << line << "\n";
					}
#endif
					safeSend(cti->socket, buf, response_len);
					delete[] buf;
				}
			}

			if ( items[1].revents & ZMQ_POLLIN) {
				char buf[10];
				size_t response_len;
				safeRecv(access_req, buf, 9, true, response_len, 0);
			}

			if ( items[0].revents & ZMQ_POLLIN) {
				zmq::message_t request;
				if (!cti->socket.recv (&request)) continue; // interrupted system call
#if 0
				if (!machine_is_ready) {
					const char *tosend = "Ignored during startup";
					safeSend(cti->socket, tosend, strlen(tosend));
					continue;
				}
#endif
				size_t size = request.size();
				char *data = (char *)malloc(size+1); // note: leaks if an exception is thrown
				memcpy(data, request.data(), size);
				data[size] = 0;
				MessageHeader mh;
				mh.start_time = microsecs();
				mh.needReply(true);
				safeSend(command_sync, data, size, mh);
				free(data);
			}

		}
		catch (std::exception e) {
			// when processing is stopped, we expect to get an exception on this interface
			// otherwise, we fail since something strange has happened
			if (!done) {
				{FileLogger fl(program_name); fl.f() << "zmq error: " << zmq_strerror(zmq_errno()) << "\n" << std::flush; }
			if (errno) {
				NB_MSG << "error during client communication: " << strerror(errno) << "\n" << std::flush;
			}
			if (zmq_errno())
				std::cerr << "zmq message: " << zmq_strerror(zmq_errno()) << "\n" << std::flush;
			else
				std::cerr << " Exception: " << e.what() << "\n" << std::flush;
			if (zmq_errno() != EINTR && zmq_errno() != EAGAIN) abort();
		}
	}
}
{FileLogger fl(program_name); fl.f() << "Client thread finished\n"; }
//monit.abort();
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
	if ((void*)cti->socket != 0) cti->socket.close();
}

