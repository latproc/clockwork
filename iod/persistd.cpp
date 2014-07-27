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
	zmq::context_t context;
	MessagingInterface::setContext(&context);

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
    store.load();
    
    SubscriptionManager subscription_manager("PERSISTENCE_CHANNEL");
    
    while (!done) {
        zmq::pollitem_t items[] = {
            { subscription_manager.setup, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 },
            { subscription_manager.subscriber, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 },
        };
        if (!subscription_manager.checkConnections()) continue;
        try {
            int rc = zmq::poll( &items[0], 2, 500);
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
            len = subscription_manager.subscriber.recv(data, 1000);
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
        }
        catch(std::exception e) {
            std::cerr << "exception " <<e.what() << "processing: " << data << "\n";
        }
    }

    return 0;
}

