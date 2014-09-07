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
#include "MessagingInterface.h"
#include "PersistentStore.h"


namespace po = boost::program_options;


bool done = false;

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


int main(int argc, const char * argv[]) {

    try {
        
        po::options_description desc("Allowed options");
        desc.add_options()
        ("help", "produce help message")
		("port", po::value<int>(), "set port number")
		("verbose", "display changes on stdout")
        ;
        po::variables_map vm;        
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);    
        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 1;
        }

		int port = 5557;
		bool verbose = false;

		if (vm.count("port")) port = vm["port"].as<int>();
		if (argc > 2 && strcmp(argv[1],"-p") == 0) {
			port = strtol(argv[2], 0, 0);
		}

		if (vm.count("verbose")) verbose = true;
		
		setup_signals();

		PersistentStore store("persist.dat");
		store.load();
		std::cout << "Listening on port " << port << "\n";
        // client
        int res;
		std::stringstream ss;
		ss << "tcp://localhost:" << port;
        zmq::context_t context (1);
        zmq::socket_t subscriber (context, ZMQ_SUB);
        res = zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "", 0);
        assert (res == 0);
        subscriber.connect(ss.str().c_str());
        //subscriber.connect("ipc://ecat.ipc");
		if (verbose) std::cout << "persistd ready\n";
        while (!done) {
            zmq::message_t update;
            subscriber.recv(&update);
			long len = update.size();
           	char *data = (char *)malloc(len+1);
           	memcpy(data, update.data(), len);
           	data[len] = 0;

			if (verbose) std::cout << data << "\n";		

			try {
        std::string cmd;
        std::list<Value> *param_list = 0;
        if (MessagingInterface::getCommand(data, cmd, &param_list)) {
					if (cmd == "PROPERTY" && param_list && param_list->size() == 3) {
						std::string property;
						int i=0;
						std::list<Value>::const_iterator iter = param_list->begin();
						Value machine_name = *iter++;
						Value property_name = *iter++;
						Value value = *iter++;
						store.insert(machine_name.asString(), property_name.asString(), value.asString().c_str());
						store.save();
					}
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
				if (param_list) delete param_list;
				free(data);
			}
			catch(std::exception e) {
				std::cerr << "exception " <<e.what() << "processing: " << data << "\n";
			}
        }
    }
    catch(std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    catch(...) {
        std::cerr << "Exception of unknown type!\n";
    }

    return 0;
}

