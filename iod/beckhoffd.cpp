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
#include "ECInterface.h"
#include "IOComponent.h"
#include "symboltable.h"
#include "ControlSystemMachine.h"
#include <sstream>
#include <stdio.h>
#include <zmq.hpp>

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <list>
#include <map>
#include "cJSON.h"
#ifndef EC_SIMULATOR
#include "tool/MasterDevice.h"
#endif

#include <stdio.h>
#include "Logger.h"
#include "IODCommands.h"
#include "Statistics.h"

int num_errors;
std::list<std::string>error_messages;
SymbolTable globals;

void usage(int argc, char *argv[]);

typedef std::map<std::string, IOComponent*> DeviceList;
DeviceList devices;

IOComponent* lookup_device(const std::string name);
void checkInputs();

IOComponent* lookup_device(const std::string name) {
    DeviceList::iterator device_iter = devices.find(name);
    if (device_iter != devices.end()) 
        return (*device_iter).second;
    return 0;
}

Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;

#ifdef EC_SIMULATOR

// in a simulated environment, we provide a way to wire components together



typedef std::list<std::string> StringList;
std::map<std::string, StringList> wiring;


void checkInputs() {
    std::map<std::string, StringList>::iterator iter = wiring.begin();
    while (iter != wiring.end()) {
        const std::pair<std::string, StringList> &link = *iter++;
        Output *device = dynamic_cast<Output*>(lookup_device(link.first));
        if (device) {
            StringList::const_iterator linked_devices = link.second.begin();
            while (linked_devices != link.second.end()) {
                Input *end_point = dynamic_cast<Input*>(lookup_device(*linked_devices++));
                SimulatedRawInput *in = 0;
                if (end_point)
                   in  = dynamic_cast<SimulatedRawInput*>(end_point->access_raw_device());
                if (in) {
                    if (device->isOn() && !end_point->isOn()) {
                        in->turnOn();
                    }
                    else if (device->isOff() && !end_point->isOff())
                        in->turnOff();
                }
            }
        }
    }
}

#endif

struct CommandThread {
    void operator()() {
        zmq::context_t context (1);
        zmq::socket_t socket (context, ZMQ_REP);
        socket.bind ("tcp://*:5555");
        IODCommand *command = 0;
        
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
	                command = new IODCommandGetStatus;
	            }
	            else if (count == 4 && ds == "SET" && params[2] == "TO") {
	                command =  new IODCommandSetStatus;
	            }
	#ifndef EC_SIMULATOR
				else if (count > 1 && ds == "EC") {
	                //std::cout << "EC: " << data << "\n";
                
	                command = new IODCommandEtherCATTool;
	            }
				else if (ds == "SLAVES") {
	                command = new IODCommandGetSlaveConfig;
				}
	            else if (count == 1 && ds == "MASTER") {
	                command = new IODCommandMasterInfo;
	            }
	#endif
	            else if (count == 2 && ds == "TOGGLE") {
	                command = new IODCommandToggle;
	            }
	            else if (count == 2 && ds == "LIST" && params[1] == "JSON") {
	                command = new IODCommandListJSON;
	            }
	            else {
	                command = new IODCommandUnknown;
	            }
	            if ((*command)(params))
	                sendMessage(socket, command->result());
	            else
	                sendMessage(socket, command->error());
	            delete command;

	            free(data);
            }
            catch (std::exception e) {
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

bool program_done = false;

int main (int argc, char const *argv[])
{
	unsigned int user_alarms = 0;
	statistics = new Statistics;
	ControlSystemMachine machine;
    Logger::instance()->setLevel(Logger::Debug);
	ECInterface::FREQUENCY=1000;

#ifndef EC_SIMULATOR
	/*std::cout << "init slaves: " << */
	collectSlaveConfig(true);
	ECInterface::instance()->activate();
#endif
	ECInterface::instance()->start();

	{
        std::list<Output *> output_list;

		if (true){
#ifndef EC_SIMULATOR
			for(int position=0; ; position++) {
				// some modules have multiple io types (eg the EK1814) and therefore
				// multiple syncmasters, we number 
				// the points from 1..n but the device numbers them 1.n,1.m,..., resetting
				// the index for each sync master.
				ECModule *module = ECInterface::findModule(position);
				if (!module) {
					break;
				}

				unsigned int num_points = 0;
				for (unsigned int i=0; i<module->sync_count; ++i)
					for (unsigned int j=0; j< module->syncs[i].n_pdos; ++j)
						num_points += module->syncs[i].pdos[j].n_entries;

				std::cout << "module " << position << " has " << num_points << " points\n";
				
				for (unsigned int bit_position = 0; bit_position<num_points; ++bit_position) {
				
					unsigned int bit_pos = bit_position;
					unsigned int direction = EC_DIR_INPUT;
					direction = module->syncs[0].dir;
					unsigned int sm_idx = 0;
					if (module->sync_count>1) {
						while (sm_idx < module->sync_count && (bit_pos+1) > module->syncs[sm_idx].n_pdos) {
							bit_pos -= module->syncs[sm_idx].n_pdos;
							direction = module->syncs[sm_idx].dir;
							++sm_idx;
						}
					}
					// default to input if the point type is not specified
					if (direction == EC_DIR_OUTPUT) {
						std::stringstream sstr;
						sstr << "BECKHOFF_" << position << "_OUT_" << bit_pos;
						const char *name_str = sstr.str().c_str();
						std::cerr << "Adding new output device " << name_str 
							<< " sm_idx: " << sm_idx << " bit_pos: " << bit_pos 
							<< " offset: " << module->offsets[sm_idx] <<  "\n";
						IOComponent::add_io_entry(name_str, module->offsets[sm_idx], bit_pos);
	            		Output *o = new Output(module->offsets[sm_idx], bit_pos);
	            		output_list.push_back(o);
	            		devices[name_str] = o;
					}
					else {
						std::stringstream sstr;
						sstr << "BECKHOFF_" << position << "_IN_" << bit_pos;
						char *name_str = strdup(sstr.str().c_str());
						std::cerr << "Adding new input device " << name_str
							<< " sm_idx: " << sm_idx << " bit_pos: " << bit_pos 
							<< " offset: " << module->offsets[sm_idx] <<  "\n";
						IOComponent::add_io_entry(name_str, module->offsets[sm_idx], bit_pos);
						Input *in = new Input(module->offsets[sm_idx], bit_pos);
						devices[name_str] = in;
						free(name_str);
					}
				}
			}
#endif
		}
 		else {
			std::cout << "EL1008 offset " << ECInterface::off_dig_in << "\n";
			std::cout << "EL2008 offset " << ECInterface::off_dig_out << "\n";
			std::cout << "EK1814_IN offset " << ECInterface::off_multi_in << "\n";
			std::cout << "EK1814_OUT offset " << ECInterface::off_multi_out << "\n";
	        for (int i=0; i<8; ++i) {
	            std::stringstream sstr;
	            sstr << "EL1008_IN_" << i;
	            IOComponent::add_io_entry(sstr.str().c_str(), ECInterface::off_dig_in, i);
	            devices[sstr.str().c_str()] = new Input(ECInterface::off_dig_out, i);
	        }
	        for (int i=0; i<8; ++i) {
	            std::stringstream sstr;
	            sstr << "EL2008_OUT_" << i;
	            IOComponent::add_io_entry(sstr.str().c_str(), ECInterface::off_dig_out, i);
	            Output *o = new Output(ECInterface::off_dig_out, i);
	            output_list.push_back(o);
	            devices[sstr.str().c_str()] = o;
	        }
	        for (int i=0; i<8; ++i) {
	            std::stringstream sstr;
	            if (i>=4) {
	                sstr << "EK1814_IN_" << (i-4);
	                IOComponent::add_io_entry(sstr.str().c_str(), ECInterface::off_multi_in, i-4);
	                devices[sstr.str().c_str()] = new Input(ECInterface::off_dig_out, i);
	            }
	            else {
	                sstr << "EK1814_OUT_" << i ;
	                IOComponent::add_io_entry(sstr.str().c_str(), ECInterface::off_multi_out, i);
	                Output *o = new Output(ECInterface::off_dig_out, i);
	                output_list.push_back(o);
	                devices[sstr.str().c_str()] = o;
	            }
	        }
	}

#ifdef EC_SIMULATOR
        wiring["EL2008_OUT_3"].push_back("EL1008_IN_1");
#endif
        std::list<Output *>::const_iterator iter = output_list.begin();
        while (iter != output_list.end()) {
            Output *o = *iter++;
			std::cout << *o << "\n";
            o->turnOff();
        }
        
        CommandThread stateMonitor;
        boost::thread monitor(boost::ref(stateMonitor));

		while (! program_done) {
			// Note: the use of pause here introduces a random variation of up to 500ns
			struct timeval start_t, end_t;
			gettimeofday(&start_t, 0);
			pause();
			gettimeofday(&end_t, 0);
			long delta = get_diff_in_microsecs(&end_t, &start_t);
			if (delta < 100) usleep(100-delta);
			while (ECInterface::sig_alarms != user_alarms) {
				ECInterface::instance()->collectState();
#ifdef EC_SIMULATOR
		        checkInputs(); // simulated wiring between inputs and outputs
#endif
			    IOComponent::processAll();
				user_alarms++;
				ECInterface::instance()->sendUpdates();
			}
			std::cout << std::flush;
		}
        stateMonitor.stop();
        monitor.join();
	}
	return 0;
}
