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

//#include "MachineInstance.h"

boost::mutex ecat_mutex;
boost::condition_variable_any ecat_polltime;

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


struct BeckhoffdToggle : public IODCommand {
	bool run(std::vector<std::string> &params);
};

    bool BeckhoffdToggle::run(std::vector<std::string> &params) {
        if (params.size() == 2) {
			DBG_MSG << "toggling " << params[1] << "\n";
			size_t pos = params[1].find('-');
			std::string machine_name = params[1];
			if (pos != std::string::npos) machine_name.erase(pos);
            Output *device = dynamic_cast<Output *>(lookup_device(machine_name));
            if (device) {
                if (device->isOn()) 
					device->turnOff();
                else if (device->isOff()) 
						device->turnOn();
				else {
					result_str = "device is neither on nor off\n";
					return false;
				}
                result_str = "OK";
                return true;
            }
			else {
				result_str = "Usage: toggle device_name";
				return false;
		    }
        }
		else {
			result_str = "Usage: toggle device_name";
			return false;
	    }
	}


struct BeckhoffdList : public IODCommand {
	bool run(std::vector<std::string> &params);
};
bool BeckhoffdList::run(std::vector<std::string> &params) {
    std::map<std::string, IOComponent*>::const_iterator iter = devices.begin();
    std::string res;
    while (iter != devices.end()) {
	    res = res + (*iter).first + "\n";
		iter++;
    }
    result_str = res;
    return true;
}

struct BeckhoffdListJSON : public IODCommand {
	bool run(std::vector<std::string> &params);
};

    bool BeckhoffdListJSON::run(std::vector<std::string> &params) {
        cJSON *root = cJSON_CreateArray();
        std::map<std::string, IOComponent*>::const_iterator iter = devices.begin();
        while (iter != devices.end()) {
			std::string name_str = (*iter).first;
            IOComponent *ioc = (*iter++).second;
			if (name_str == "!") continue; //TBD
			cJSON *node = cJSON_CreateObject();
    		cJSON_AddStringToObject(node, "name", name_str.c_str());
			cJSON_AddStringToObject(node, "class", ioc->type());
			cJSON_AddNumberToObject(node, "value", ioc->value());
			if (strcmp(ioc->type(), "Output") == 0)
    				cJSON_AddStringToObject(node, "tab", "Outputs");
			else
    				cJSON_AddStringToObject(node, "tab", "Inputs");

			cJSON_AddStringToObject(node, "state", ioc->getStateString());
//			if (ioc->isOn())
//    			cJSON_AddStringToObject(node, "state", "on");
//			else if (ioc->isOff())
//    			cJSON_AddStringToObject(node, "state", "off");
//			else
//    			cJSON_AddStringToObject(node, "state", "unknown");
		cJSON_AddTrueToObject(node, "enabled");

   		    cJSON_AddItemToArray(root, node);
        }
        char *res = cJSON_Print(root);
        cJSON_Delete(root);
        bool done;
        if (res) {
            result_str = res;
            free(res);
            done = true;
        }
        else {
            error_str = "JSON error";
            done = false;
        }
        return done;
    }


struct BeckhoffdProperty : public IODCommand {
	BeckhoffdProperty(const char *data) : details(data) { };
	bool run(std::vector<std::string> &params);
	std::string details;
};
bool BeckhoffdProperty::run(std::vector<std::string> &params) {
    //if (params.size() == 4) {
	IOComponent *m = lookup_device(params[1]);
    if (m) {
        if (params.size() == 3) {
            long x;
            char *p;
            x = strtol(params[2].c_str(), &p, 0);
            if (*p == 0)
                m->setValue(x);
            else {
                result_str = "Require integer parameter";
                return false;
			}
        }
        else {
            result_str = "usage: PROPERTY name value";
            return false;
		}

        result_str = "OK";
        return true;
    }
    else {
        result_str = "Unknown device";
        return false;
    }
}

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
	                command = new BeckhoffdToggle;
	            }
	            else if (ds == "PROPERTY") {
	                command = new BeckhoffdProperty(data);
	            }
	            else if (count == 1 && ds == "LIST") {
	                command = new BeckhoffdList;
	            }
	            else if (count >= 2 && ds == "LIST" && params[1] == "JSON") {
	                command = new BeckhoffdListJSON;
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
                //std::cout << e.what() << "\n";
		//usleep(200000);
                //exit(1);
            }
        } 
    }
    CommandThread() : done(false) {
        
    }
    void stop() { done = true; }
    bool done;
};


void generateIOComponentMappings() {
        std::list<Output *> output_list;

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
				
				unsigned int sm_idx = 0;
				unsigned int pdo_idx = 0;
				unsigned int entry_idx = 0;
				unsigned int bit_pos = 0;
				unsigned int offset_idx = 0;
				// add io entries to for each point.
				unsigned int pos = 0; 
				while (pos<num_points) {

					if (sm_idx >= module->sync_count) {
						std::cerr  << "out of bits trying to find offset for point " << pos << "\n";
						break;
					}
					if (module->syncs[sm_idx].n_pdos == 0) {
						std::cout << "sm_idx: " << sm_idx << " has no pdos\n";
						++sm_idx;
						continue;
					}
					unsigned int direction = module->syncs[sm_idx].dir;

					unsigned int bitlen = module->syncs[sm_idx].pdos[pdo_idx].entries[entry_idx].bit_length;

					if (true || module->syncs[sm_idx].pdos[pdo_idx].entries[entry_idx].index) {
					if (direction == EC_DIR_OUTPUT) {
						std::stringstream sstr;
						sstr << "EC_" << std::setfill('0') 
							<< std::setw(2) << position << "_OUT_" 
							<< std::setfill('0') << std::setw(2) << (pos+1);
						const char *name_str = sstr.str().c_str();
						std::cerr << "Adding new output device " << name_str 
							<< " sm_idx: " << sm_idx 
							<< " name: " << module->entry_details[offset_idx].name
							<< " bit_pos: " << module->bit_positions[offset_idx] 
							<< " offset: " << module->offsets[offset_idx] 
							<< " bitlen: " << bitlen <<  "\n";
						IOAddress addr(IOComponent::add_io_entry(name_str, module->offsets[offset_idx], 
								module->bit_positions[offset_idx], offset_idx, bitlen));
						if (bitlen == 1) {
	            			Output *o = new Output(addr);
							o->setName(name_str);
	            			output_list.push_back(o);
	            			devices[name_str] = o;
						}
						else {
	            			AnalogueOutput *o = new AnalogueOutput(addr);
							o->setName(name_str);
	            			output_list.push_back(o);
	            			devices[name_str] = o;
						}
					}
					else {
						std::stringstream sstr;
						sstr << "EC_" << std::setfill('0') << std::setw(2)
							<< position << "_IN_" << std::setfill('0') 	
							<< std::setw(2) << (pos+1);
						char *name_str = strdup(sstr.str().c_str());
						std::cerr << "Adding new input device " << name_str
							<< " sm_idx: " << sm_idx 
							<< " name: " << module->entry_details[offset_idx].name
							<< " bit_pos: " << module->bit_positions[offset_idx]
							<< " offset: " << module->offsets[offset_idx] 
							<<  " bitlen: " << bitlen << "\n";
						IOAddress addr(IOComponent::add_io_entry(name_str, module->offsets[offset_idx], 
							module->bit_positions[offset_idx], offset_idx, bitlen));
						if (bitlen == 1) {
							Input *in = new Input(addr);
							in->setName(name_str);
							devices[name_str] = in;
							free(name_str);
						}
						else {
							AnalogueInput *in = new AnalogueInput(addr);
							in->setName(name_str);
							devices[name_str] = in;
							free(name_str);
						}
					}
					}
					//while (sm_idx < module->sync_count && (bit_pos+1) > module_bits) {
					bit_pos += bitlen;
					if (++entry_idx >= module->syncs[sm_idx].pdos[pdo_idx].n_entries) {
						entry_idx = 0;
						if (++pdo_idx >= module->syncs[sm_idx].n_pdos) {
							pdo_idx = 0;
							++sm_idx;
							bit_pos = 0;
						}
					}
					++pos;
					++offset_idx;
				}
			}
#endif

#ifdef EC_SIMULATOR
        wiring["EL2008_OUT_3"].push_back("EL1008_IN_1");
#endif
        std::list<Output *>::const_iterator iter = output_list.begin();
        while (iter != output_list.end()) {
            Output *o = *iter++;
			std::cout << *o << "\n";
            o->turnOff();
        }
		std::cout << std::flush;
	}

void displayEtherCATModulePDOOffsets() {
    std::list<ECModule *>::const_iterator iter = ECInterface::modules.begin();
    int module_offset_idx = 0;
    while (iter != ECInterface::modules.end()){
	    ECModule *m = *iter++;
        m->operator<<(std::cout) << "\n";
        for(unsigned int i=0; i<m->sync_count; ++i) {
            for (unsigned int j = 0; j<m->syncs[i].n_pdos; ++j) {
                for (unsigned int k = 0; k < m->syncs[i].pdos[j].n_entries; ++k) {
                    std::cout << m->name << " pdo offset " << i
                        << " " << m->offsets[module_offset_idx]
                        << " " << m->bit_positions[module_offset_idx]
                        << "\n";
                    ++module_offset_idx;
                }
            }
        }
    }
}
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
	ECInterface::FREQUENCY=5000;

#ifndef EC_SIMULATOR
	collectSlaveConfig(true); // load slave information from the EtherCAT master and configure the domain
	ECInterface::instance()->activate();
#endif
	generateIOComponentMappings();
	ECInterface::instance()->start();

    CommandThread stateMonitor;
    boost::thread monitor(boost::ref(stateMonitor));

	while (! program_done) {
		// Note: the use of pause here introduces a random variation of up to 500ns
		struct timeval start_t, end_t;
		gettimeofday(&start_t, 0);
//		pause();
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
	return 0;
}
