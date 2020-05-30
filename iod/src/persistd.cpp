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
#include <iterator>
#include <stdio.h>
#include <boost/program_options.hpp>
#include <zmq.hpp>
#include <sstream>
#include <string.h>
#include "Logger.h"
#include <inttypes.h>
#include <fstream>
#include "symboltable.h"
#include <list>
#include <utility>
#include <boost/foreach.hpp>
#include <signal.h>
#include <sys/time.h>
#include "cJSON.h"
#include "value.h"
#include "symboltable.h"
#include "MessageEncoding.h"
#include "PersistentStore.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "ConnectionManager.h"


namespace po = boost::program_options;


bool done = false;
zmq::socket_t *cmd_socket = 0;

static void finish(int sig)
{
	struct sigaction sa;
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	done = true;
	exit(0);
}

bool setup_signals()
{
	struct sigaction sa;
	sa.sa_handler = finish;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGTERM, &sa, 0) || sigaction(SIGINT, &sa, 0)) {
			return false;
	}
	return true;
}

// monitor the connection to iod and each time the connection
// is remade a request is made for the state of persistent
// objects.
static bool need_refresh = false;

class SetupConnectMonitor : public EventResponder {
public:
	void operator()(const zmq_event_t &event_, const char* addr_) {
			need_refresh = true;
	}
};

class SetupDisconnectMonitor : public EventResponder {
public:
	void operator()(const zmq_event_t &event_, const char* addr_) {
		done = true;
	}
};


std::set<std::string>ignored_properties;

class ExitMessage {
public:
	ExitMessage(const char *msg) :message(msg) {}
	~ExitMessage() { std::cout << message << "\n"; }
	std::string message;
};

bool loadActiveData(PersistentStore &store, const char *initial_settings)
{
	std::cout << "loading data from clockwork\n";
	ExitMessage em("load data done");
	cJSON *obj = cJSON_Parse(initial_settings);
	if (!obj) return false;
	int num_entries = cJSON_GetArraySize(obj);
	if (num_entries)
	{
		// persitent data is provided as an array of object,
		// each of which is
		// (string name, string property, Value value)
		for (int i=0; i<num_entries; ++i)
		{
			cJSON *item = cJSON_GetArrayItem(obj, i);
			if (item->type == cJSON_Array)
			{
				cJSON *fld = cJSON_GetArrayItem(item, 0);
				assert(fld->type == cJSON_String);
				Value machine = fld->valuestring;

				fld = cJSON_GetArrayItem(item, 1);
				assert(fld->type == cJSON_String);
				Value property = fld->valuestring;

				if (!ignored_properties.count(property.asString())) {
					cJSON *json_val = cJSON_GetArrayItem(item, 2);
					switch (json_val->type) {
						case cJSON_String:
							store.insert(machine.asString(), property.asString(), json_val->valuestring);
							break;
						case cJSON_Number:
							if (json_val->valueNumber.kind == cJSON_Number_int_t)
								store.insert(machine.asString(), property.asString(), json_val->valueNumber.val._int);
							else
								store.insert(machine.asString(), property.asString(), json_val->valueNumber.val._double);
							break;
						case cJSON_True:
							store.insert(machine.asString(), property.asString(), true);
							break;
						case cJSON_False:
							store.insert(machine.asString(), property.asString(), false);
							break;
						default:
							assert(false);
					}
				}
			}
			else
			{
				char *node = cJSON_Print(item);
				std::cerr << "item " << i << " is not of the expected format: " << node << "\n";
				free(node);
			}
		}
		return true;
	}
	else return true;
}


void CollectPersistentStatus(PersistentStore &store) {
	std::string initial_settings;
	int tries = 3;
	do
	{
		std::cout << "-------- Collecting IO Status ---------\n" << std::flush;
		if (cmd_socket && sendMessage("PERSISTENT STATUS", *cmd_socket, initial_settings)) {
			if ( strncasecmp(initial_settings.c_str(), "ignored", strlen("ignored")) != 0)
			{
				if (loadActiveData(store, initial_settings.c_str())) store.save();
				break;
			}
			else
				sleep(2);
		}
		else {
				sleep(1);
		}
		if (--tries <= 0) {
			NB_MSG << "failed to collect status, exiting to reconnect\n";
			exit(0);
		}
	}
	while (!done);
}

int main(int argc, const char * argv[]) {
	char *pn = strdup(argv[0]);
	program_name = strdup(basename(pn));
	free(pn);

	zmq::context_t context;
	MessagingInterface::setContext(&context);
    
	ignored_properties.insert("tab");
	ignored_properties.insert("type");
	ignored_properties.insert("name");
	ignored_properties.insert("image");
	ignored_properties.insert("class");
	ignored_properties.insert("state");
	ignored_properties.insert("export");
	ignored_properties.insert("startup_enabled");
	ignored_properties.insert("NAME");
	ignored_properties.insert("STATE");
	ignored_properties.insert("PERSISTENT");
	ignored_properties.insert("POLLING_DELAY");
	ignored_properties.insert("TRACEABLE");
	ignored_properties.insert("IOTIME");

	po::options_description desc("Allowed options");
	desc.add_options()
		("help", "produce help message")
		("verbose", "display changes on stdout")
	;
	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);
	if (vm.count("help")) {
		std::cout << desc << "\n";
		return 1;
	}

	bool verbose = false;
	if (vm.count("verbose")) verbose = true;

	setup_signals();

	PersistentStore store("persist.dat");
	//store.load(); // disabled load store since we take it from clockwork now

	SubscriptionManager subscription_manager("PERSISTENCE_CHANNEL", eCLOCKWORK);
	SetupConnectMonitor connect_responder;
	SetupDisconnectMonitor disconnect_responder;
	cmd_socket = &subscription_manager.setup();
	subscription_manager.monit_setup->addResponder(ZMQ_EVENT_CONNECTED, &connect_responder);
	subscription_manager.monit_setup->addResponder(ZMQ_EVENT_DISCONNECTED, &disconnect_responder);

	while (!done) {
		zmq::pollitem_t items[] = {
			{ (void*)subscription_manager.setup(), 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 },
			{ (void*)subscription_manager.subscriber(), 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 },
		};
		if (!subscription_manager.checkConnections()) {
			usleep(100);
			continue;
		}
		try {
			int rc = zmq::poll( &items[0], 2, 500);
			if (need_refresh) {
				CollectPersistentStatus(store);
				need_refresh = false;
			}
			if (rc == 0) continue;
		}
		catch(zmq::error_t e) {
				if (errno == EINTR || errno == EAGAIN) continue;
		}
		if ( !(items[1].revents & ZMQ_POLLIN) ) continue;
		//zmq::message_t update;
		char data[1000];
		size_t len = 0;
		try {
			len = subscription_manager.subscriber().recv(data, 1000, ZMQ_DONTWAIT);
			if (!len) continue;
		}
		catch (zmq::error_t e) {
				if (errno == EINTR) continue;

		}
		data[len] = 0;

		if (verbose) std::cout << data << "\n";

		try {
				std::string cmd;
				std::list<Value> *param_list = 0;
				if (MessageEncoding::getCommand(data, cmd, &param_list)) {
					if (cmd == "PROPERTY" && param_list && param_list->size() == 3) {
						std::string property;
						std::list<Value>::const_iterator iter = param_list->begin();
						Value machine_name = *iter++;
						Value property_name = *iter++;
						Value value = *iter++;
						store.insert(machine_name.asString(), property_name.asString(), value);
						store.save();
					}
				else
					std::cerr << "unexpected command: " << cmd << " sent to persistd\n";
				}
				else {
						std::istringstream iss(data);
						std::string property, op, value;
						iss >> property >> op >> value;
						std::string machine_name;
						store.split(machine_name, property);
						store.insert(machine_name, property, value.c_str());
						store.save();
				}
				if (param_list) { delete param_list; param_list = 0; }
		}
		catch(std::exception e) {
				std::cerr << "exception " <<e.what() << " processing: " << data << "\n";
		}
	}

	return 0;
}

