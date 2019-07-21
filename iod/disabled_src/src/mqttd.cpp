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

#include <unistd.h>
#include "MQTTInterface.h"
#include "MQTTDCommand.h"
#include "MQTTDCommands.h"
#include "symboltable.h"
#include <sstream>
#include <stdio.h>
#include <zmq.hpp>

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <list>
#include <map>
#include "cJSON.h"

#include <stdio.h>
#include "Logger.h"
#include "Statistics.h"

boost::mutex ecat_mutex;
boost::condition_variable_any ecat_polltime;

int num_errors;
std::list<std::string>error_messages;
SymbolTable globals;

void usage(int argc, char *argv[]);

typedef std::map<std::string, MQTTModule*> DeviceList;
DeviceList devices;

void checkInputs();

MQTTModule* lookup_device(const std::string name) {
    DeviceList::iterator device_iter = devices.find(name);
    if (device_iter != devices.end()) 
        return (*device_iter).second;
    return 0;
}

Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;


struct CommandThread {
    void operator()() {
        zmq::socket_t socket (context, ZMQ_REP);
        socket.bind ("tcp://*:5555");
        MQTTDCommand *command = 0;
        
        while (!done) {
            zmq::message_t request;
            try {
	            //  Wait for next request from client
	            socket.recv (&request);
	            size_t size = request.size();
	            char *data = (char *)malloc(size+1);
	            memcpy(data, request.data(), size);
	            data[size] = 0;
	            //std::cout << "Received " << data << std::endl;
	            std::istringstream iss(data);
	            std::list<std::string> parts;
	            std::string ds;
	            int count = 0;
	            while (iss >> ds) {
	                parts.push_back(ds);
	                ++count;
	            }
            
	            std::vector<std::string> params(0);
	            std::copy(parts.begin(), parts.end(), std::back_inserter(params));
	            ds = params[0];
	            if (ds == "GET" && count>1) {
	                command = new MQTTDCommandGetStatus;
	            }
	            else if (count == 4 && ds == "SET") {
	                command =  new MQTTDCommandSetStatus;
	            }
	            else if (count == 2 && ds == "LIST" && params[1] == "JSON") {
	                command = new MQTTDCommandListJSON;
	            }
	            else {
	                command = new MQTTDCommandUnknown;
	            }
	            if ((*command)(params))
	                sendMessage(socket, command->result());
	            else
	                sendMessage(socket, command->error());
	            delete command;

	            free(data);
            }
            catch (const std::exception &e) {
                std::cout << e.what() << "\n";
				usleep(200000);
               //exit(1);
            }
        } 
    }
    CommandThread() : done(false) {
        
    }
    void stop() { done = true; }
    bool done;
};

void usage(int argc, char const *argv[])
{
    fprintf(stderr, "Usage: %s\n", argv[0]);
}

long get_diff_in_microsecs(struct timeval *now, struct timeval *then) {
    uint64_t t = (now->tv_sec - then->tv_sec);
    t = t * 1000000 + (now->tv_usec - then->tv_usec);
    return t;
}


bool program_done = false;

int main (int argc, char const *argv[])
{
	unsigned int user_alarms = 0;
	statistics = new Statistics;
    Logger::instance()->setLevel(Logger::Debug);
	MQTTInterface::FREQUENCY=10;

	MQTTInterface::instance()->start();

    CommandThread stateMonitor;
    boost::thread monitor(boost::ref(stateMonitor));

    while (! program_done) {
        struct timeval start_t, end_t;
        gettimeofday(&start_t, 0);
        pause();
        gettimeofday(&end_t, 0);
        long delta = get_diff_in_microsecs(&end_t, &start_t);
        if (delta < 100) usleep(100-delta);
        while (MQTTInterface::sig_alarms != user_alarms) {
            MQTTInterface::instance()->collectState();
            MQTTInterface::processAll();
            user_alarms++;
            MQTTInterface::instance()->sendUpdates();
        }
        std::cout << std::flush;
    }
    stateMonitor.stop();
    monitor.join();
	return 0;
}
