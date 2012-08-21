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
#include "IODCommand.h"

#ifndef EX_SIMULATOR
#include <master/globals.h>
const ec_code_msg_t *soe_error_codes;
#endif


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

#else

// in a real environment we can look for devices on the bus

ec_pdo_entry_info_t *c_entries = 0;
ec_pdo_info_t *c_pdos = 0;
ec_sync_info_t *c_syncs = 0;


cJSON *generateSlaveCStruct(MasterDevice &m, const ec_ioctl_slave_t &slave, bool reconfigure)
{
    ec_ioctl_slave_sync_t sync;
    ec_ioctl_slave_sync_pdo_t pdo;
    ec_ioctl_slave_sync_pdo_entry_t entry;
    unsigned int i, j, k, pdo_pos = 0, entry_pos = 0;

	cJSON *root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "position", slave.position);
	cJSON_AddNumberToObject(root, "vendor_id", slave.vendor_id);
	cJSON_AddNumberToObject(root, "revision_number", slave.revision_number);
	cJSON_AddNumberToObject(root, "drawn_current", slave.current_on_ebus);
	char *name = strdup(slave.name);
	int name_len = strlen(name);
	for (int i=0; i<name_len; ++i) name[i] &= 127;
	cJSON_AddStringToObject(root, "name", name);
	free(name);

	cJSON *syncs = cJSON_CreateObject();
        if (slave.sync_count) {
		// add pdo entries for this slave
		// note the assumptions here about the maximum number of entries (32), pdos(32) and syncs (8) we expect
		const int c_entries_size = sizeof(ec_pdo_entry_info_t) * 32;
		c_entries = (ec_pdo_entry_info_t *) malloc(c_entries_size);
		memset(c_entries, 0, c_entries_size);

		const int c_pdos_size = sizeof(ec_pdo_info_t) * 32;
		c_pdos = (ec_pdo_info_t *) malloc(c_pdos_size);
		memset(c_pdos, 0, c_pdos_size);

		const int c_syncs_size = sizeof(ec_sync_info_t) * 32;
		c_syncs = (ec_sync_info_t *) malloc(c_syncs_size);
		memset(c_syncs, 0, c_syncs_size);

	    for (i = 0; i < slave.sync_count; i++) {
	        m.getSync(&sync, slave.position, i);
			c_syncs[i].index = sync.sync_index;
			c_syncs[i].dir = EC_READ_BIT(&sync.control_register, 2) ? EC_DIR_OUTPUT : EC_DIR_INPUT;
			c_syncs[i].n_pdos = (unsigned int) sync.pdo_count;
			cJSON_AddNumberToObject(syncs, "index", c_syncs[i].index);
			cJSON_AddStringToObject(syncs, "direction", (c_syncs[i].dir == EC_DIR_OUTPUT) ? "Output" : "Input");
			if (sync.pdo_count)
				c_syncs[i].pdos = c_pdos + pdo_pos;
			else
				c_syncs[i].pdos = 0;
			c_syncs[i].watchdog_mode = EC_READ_BIT(&sync.control_register, 6) ? EC_WD_ENABLE : EC_WD_DISABLE;
		
	        for (j = 0; j < sync.pdo_count; j++) {
	            m.getPdo(&pdo, slave.position, i, j);

				c_pdos[j + pdo_pos].index = pdo.index;
				c_pdos[j + pdo_pos].n_entries = (unsigned int) pdo.entry_count;
				if (pdo.entry_count) 
					c_pdos[j + pdo_pos].entries = c_entries + entry_pos;
				else
					c_pdos[j + pdo_pos].entries = 0;
			
	            for (k = 0; k < pdo.entry_count; k++) {
	                m.getPdoEntry(&entry, slave.position, i, j, k);

					c_entries[k + entry_pos].index = entry.index;
					c_entries[k + entry_pos].subindex = entry.subindex;
					c_entries[k + entry_pos].bit_length = entry.bit_length;
	            }
	            entry_pos += pdo.entry_count;
	        }

	        pdo_pos += sync.pdo_count;

	    }
		c_syncs[slave.sync_count].index = 0xff;
	}
	else {
		c_syncs = 0;
		c_pdos = 0;
		c_entries = 0;
	}
	if (reconfigure) {
		ECModule *module = new ECModule();
		module->name = slave.name;
		module->alias = 0;
		module->position = slave.position;
		module->vendor_id = slave.vendor_id;
		module->product_code = slave.product_code;
		module->syncs = c_syncs;
		module->pdos = c_pdos;
		module->pdo_entries = c_entries;
		module->sync_count = slave.sync_count;
		if (!ECInterface::instance()->addModule(module, true)) delete module; // module may be already registered
	}
	if (slave.sync_count)
		cJSON_AddItemToObject(root, "sync_managers", syncs);
	else
		cJSON_Delete(syncs);
	
	//std::stringstream result;
	//result << "slave: " << slave.position << "\t"
	//	<< "syncs: " << i << " pdos: " << pdo_pos << " entries: " << entry_pos << "\n";
	//result << cJSON_Print(root);
	//return result.str();
	return root;
}

char *collectSlaveConfig(bool reconfigure)
{
	std::stringstream res;
	cJSON *root = cJSON_CreateArray();
    MasterDevice m(0);
    m.open(MasterDevice::Read);

    ec_ioctl_master_t master;
    ec_ioctl_slave_t slave;
    m.getMaster(&master);

	for (unsigned int i=0; i<master.slave_count; i++) {
		m.getSlave(&slave, i);
        cJSON_AddItemToArray(root, generateSlaveCStruct(m, slave, reconfigure));
    }
	return cJSON_Print(root);
}

#endif

struct CommandGetStatus : public IODCommand {
    bool run(std::vector<std::string> &params) {
        if (params.size() == 2) {
            IOComponent *device = lookup_device(params[1]);
            if (device) {
                done = true;
                result_str = strdup(device->getStateString());
            }
            else
                error_str = strdup("Not Found");
        }
        return done;
    }
};

struct CommandSetStatus : public IODCommand {
    bool run(std::vector<std::string> &params) {
        if (params.size() == 4) {
            std::string ds = params[1];
            Output *device = dynamic_cast<Output *>(lookup_device(ds));
            if (device) {
                if (params[3] == "on")
                    device->turnOn();
                else if (params[3] == "off")
                    device->turnOff();
                result_str = device->getStateString();
                return true;
            }
            else {
                //  Send reply back to client
                const char *msg_text = "Not found: ";
                size_t len = strlen(msg_text) + ds.length();
                char *text = (char *)malloc(len+1);
                sprintf(text, "%s%s", msg_text, ds.c_str());
                error_str = text;
                return false;
            }
        }
        error_str = "Usage: SET device TO state";
        return false;
    }
};

#ifndef EC_SIMULATOR

struct CommandGetSlaveConfig : public IODCommand {
    bool run(std::vector<std::string> &params) {
        char *res = collectSlaveConfig(false);
        if (res) {
            result_str = res;
            return true;
        }
        else {
            error_str = "JSON Error";
            return false;
        }
    }
};

struct CommandMasterInfo : public IODCommand {
    bool run(std::vector<std::string> &params) {
        //const ec_master_t *master = ECInterface::instance()->getMaster();
        const ec_master_state_t *master_state = ECInterface::instance()->getMasterState();
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "slave_count", master_state->slaves_responding);
        cJSON_AddNumberToObject(root, "link_up", master_state->link_up);
        
        char *res = cJSON_Print(root);
        bool done;
        if (res) {
            result_str = res;
            done = true;
        }
        else {
            error_str = "JSON error";
            done = false;
        }
        cJSON_Delete(root);
        return done;
    }
};

#endif

struct CommandToggle : public IODCommand {
    bool run(std::vector<std::string> &params) {
        if (params.size() == 2) {
            Output *device = dynamic_cast<Output *>(lookup_device(params[1]));
            if (device) {
                if (device->isOn()) device->turnOff();
                else if (device->isOff()) device->turnOn();
                result_str = "OK";
                return true;
            }
            else {
                error_str = "Unknown device";
                return false;
            }
        }
        else {
            error_str = "Usage: toggle device_name";
            return false;
        }
    }
};

cJSON *printIOComponentToJSON(IOComponent *m, std::string prefix = "") {
    cJSON *node = cJSON_CreateObject();
	std::stringstream ss;
	ss << *m << std::flush;
    std::string name_str = ss.str();
    if (prefix.length()!=0) {
        std::stringstream ss;
        ss << prefix << '-' << *m;
        name_str = ss.str();
    }
    cJSON_AddStringToObject(node, "name", name_str.c_str());
    cJSON_AddStringToObject(node, "class", m->type());
    cJSON_AddStringToObject(node, "type", m->type());
    cJSON_AddStringToObject(node, "tab", m->type());
    cJSON_AddStringToObject(node, "state", m->getStateString());
    return node;
}


struct CommandListJSON : public IODCommand {
    bool run(std::vector<std::string> &params) {
        cJSON *root = cJSON_CreateArray();
		
		DeviceList::iterator iter = devices.begin();
        while (iter != devices.end()) {
			IOComponent* m = (*iter).second;
            cJSON_AddItemToArray(root, printIOComponentToJSON(m));
			iter++;
        }
        char *res = cJSON_Print(root);
        bool done;
        if (res) {
            result_str = res;
            done = true;
        }
        else {
            error_str = "JSON error";
            done = false;
        }
        cJSON_Delete(root);
        return done;
    }
};

struct CommandUnknown : public IODCommand {
    bool run(std::vector<std::string> &params) {
        std::stringstream ss;
        ss << "Unknown command: ";
        std::ostream_iterator<std::string> oi(ss, " ");
        ss << std::flush;
        error_str = strdup(ss.str().c_str());
        return false;
    }
};

void sendMessage(zmq::socket_t &socket, const char *message) {
    const char *msg = (message) ? message : "";
    size_t len = strlen(msg);
    zmq::message_t reply (len);
    memcpy ((void *) reply.data (), msg, len);
    socket.send (reply);
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
	                command = new CommandGetStatus;
	            }
	            else if (count == 4 && ds == "SET" && params[2] == "TO") {
	                command =  new CommandSetStatus;
	            }
	#ifndef EC_SIMULATOR
				else if (ds == "SLAVES") {
	                command = new CommandGetSlaveConfig;
				}
	            else if (count == 1 && ds == "MASTER") {
	                command = new CommandMasterInfo;
	            }
	#endif
	            else if (count == 2 && ds == "TOGGLE") {
	                command = new CommandToggle;
	            }
	            else if (count == 2 && ds == "LIST" && params[1] == "JSON") {
	                command = new CommandListJSON;
	            }
	            else {
	                command = new CommandUnknown;
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

int main (int argc, char const *argv[])
{
	unsigned int user_alarms = 0;
	ControlSystemMachine machine;
    Logger::instance()->setLevel(Logger::Debug);
	ECInterface::FREQUENCY=1000;

#ifndef EC_SIMULATOR
	/*std::cout << "init slaves: " << */
	collectSlaveConfig(true) /*<< "\n"*/;
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

		while (1) {
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
