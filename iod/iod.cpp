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
#include "ControlSystemMachine.h"
#include "IOComponent.h"
#include <sstream>
#include <stdio.h>
#include <zmq.hpp>
#include <sys/stat.h>
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <list>
#include <map>
#include <utility>
#include <fstream>
#include "cJSON.h"
#ifndef EC_SIMULATOR
#include <MasterDevice.h>
#endif

#define __MAIN__
#include "latprocc.h"
#include "options.h"
#include <stdio.h>
#include "Logger.h"
#include "IODCommand.h"
#include "Dispatcher.h"
#include "Statistic.h"
#include "symboltable.h"
#include "DebugExtra.h"
#include "Scheduler.h"
#include "PredicateAction.h"
#include "ModbusInterface.h"

extern int yylineno;
extern int yycharno;
const char *yyfilename = 0;
extern FILE *yyin;
int num_errors = 0;
std::list<std::string>error_messages;
SymbolTable globals;

bool program_done = false;
bool machine_is_ready = false;

int yyparse();
int yylex(void);
void usage(int argc, char *argv[]);
void displaySymbolTable();

struct Statistics {
	Statistic io_scan_time;
	Statistic points_processing;
	Statistic machine_processing;
	Statistic dispatch_processing;
	Statistic auto_states;	
	Statistic web_processing;

	Statistics() : 
	        io_scan_time("I/O Scan            "),
	   points_processing("POINTS sync         "),
	  machine_processing("Machine Processing  "),
	 dispatch_processing("Dispatch Processing "),
	         auto_states("Stable States       "),
	      web_processing("Web Processing      ")
	{}
	
};

Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;

static boost::mutex q_mutex;
static boost::condition_variable_any cond;


typedef std::map<std::string, IOComponent*> DeviceList;
DeviceList devices;

//IOComponent* lookup_device(const std::string name);
void checkInputs();

IOComponent* lookup_device(const std::string name) {
    DeviceList::iterator device_iter = devices.find(name);
    if (device_iter != devices.end()) 
        return (*device_iter).second;
    return 0;
}

static void list_directory( const std::string &pathToCheck, std::list<std::string> &file_list)
{
    const boost::filesystem::directory_iterator endMarker;
    
    for (boost::filesystem::directory_iterator file (pathToCheck);
         file != endMarker;
         ++file)
    {
        struct stat file_stat;
        int err = stat((*file).path().native().c_str(), &file_stat);
        if (err == -1) {
            std::cerr << "Error: " << strerror(errno) << " checking file type for " << (*file) << "\n";
        }
        else if (file_stat.st_mode & S_IFDIR){
            list_directory((*file).path().native().c_str(), file_list);
        }
        else if (boost::filesystem::exists (*file) && (*file).path().extension() == ".lpc")
        {
            file_list.push_back( (*file).path().native() );
        }
    }
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

std::string tool_main(int argc, char **argv);

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

struct IODCommandGetStatus : public IODCommand {
    bool run(std::vector<std::string> &params) {
        if (params.size() == 2) {
	    MachineInstance *machine = MachineInstance::find(params[1].c_str());
	    if (machine) {
		done = true;
		result_str = strdup(machine->getCurrentStateString());
	    }
            else
                error_str = strdup("Not Found");
        }
        return done;
    }
};

struct IODCommandSetStatus : public IODCommand {
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

struct IODCommandEtherCATTool : public IODCommand {
    bool run(std::vector<std::string> &params) {
        if (params.size() > 1) {
            int argc = params.size();
            char **argv = (char**)malloc((argc+1) * sizeof(char*));
            for (int i=0; i<argc; ++i) {
                argv[i] = strdup(params[i].c_str());
            }
            argv[argc] = 0;
            std::string res = tool_main(argc, argv);
            std::cout << res << "\n";
            for (int i=0; i<argc; ++i) {
                free(argv[i]);
            }
            free(argv);
            result_str = res;
            return true;
        }
        else {
            error_str = "Usage: EC command [params]";
            return false;
        }
    }
};

struct IODCommandGetSlaveConfig : public IODCommand {
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

struct IODCommandMasterInfo : public IODCommand {
    bool run(std::vector<std::string> &params) {
        //const ec_master_t *master = ECInterface::instance()->getMaster();
        const ec_master_state_t *master_state = ECInterface::instance()->getMasterState();
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "slave_count", master_state->slaves_responding);
        cJSON_AddNumberToObject(root, "link_up", master_state->link_up);
		std::stringstream ss;
		statistics->io_scan_time.report(ss);
		statistics->points_processing.report(ss);
		statistics->machine_processing.report(ss);
		statistics->dispatch_processing.report(ss);
		statistics->auto_states.report(ss);
		Statistic::reportAll(ss);
		ss << std::flush;
		cJSON_AddStringToObject(root, "statistics", ss.str().c_str());	
        
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

struct IODCommandEnable : public IODCommand {
    bool run(std::vector<std::string> &params) {
		std::cout << "received iod command ENABLE " << params[1] << "\n";
        if (params.size() == 2) {
			DBG_MSG << "enabling " << params[1] << "\n";
			MachineInstance *m = MachineInstance::find(params[1].c_str());
			if (m && !m->enabled()) m->enable();
			result_str = "OK";
			return true;
		}
		error_str = "Failed to find machine";
		return false;
	}
};

struct IODCommandResume : public IODCommand {
    bool run(std::vector<std::string> &params) {
		std::cout << "received iod command RESUME " << params[1] << "\n";
        if (params.size() == 2) {
			DBG_MSG << "resuming " << params[1] << "\n";
			MachineInstance *m = MachineInstance::find(params[1].c_str());
			if (m && !m->enabled()) m->resume();
			result_str = "OK";
			return true;
		}
        else if (params.size() == 4 && params[2] == "AT") {
			DBG_MSG << "resuming " << params[1] << " at state "  << params[3] << "\n";
			MachineInstance *m = MachineInstance::find(params[1].c_str());
			if (m && !m->enabled()) m->resume();
			result_str = "OK";
			return true;
		}
		error_str = "Failed to find machine";
		return false;
	}
};

struct IODCommandDisable : public IODCommand {
    bool run(std::vector<std::string> &params) {
		std::cout << "received iod command DISABLE " << params[1] << "\n";
        if (params.size() == 2) {
			DBG_MSG << "disabling " << params[1] << "\n";
			MachineInstance *m = MachineInstance::find(params[1].c_str());
			if (m && m->enabled()) m->disable();
			result_str = "OK";
			return true;
		}
		error_str = "Failed to find machine";
		return false;
	}
};


struct IODCommandToggle : public IODCommand {
    bool run(std::vector<std::string> &params) {
        if (params.size() == 2) {
			DBG_MSG << "toggling " << params[1] << "\n";
			size_t pos = params[1].find('-');
			std::string machine_name = params[1];
			if (pos != std::string::npos) machine_name.erase(pos);
		    MachineInstance *m = MachineInstance::find(machine_name.c_str());
		    if (m) {
				if (pos != std::string::npos) {
					machine_name = params[1].substr(pos+1);
					m = m->lookup(machine_name);
					if (!m) {
						error_str = "No such machine";
						return false;
					}
				}
				if (!m->enabled()) {
					error_str = "Device is disabled";
					return false;
				}
		  	 	if (m->_type != "POINT") {
					if (m->_type == "FLAG") {
						if (m->getCurrent().getName() == "on")
							m->setState("off");
						else
							m->setState("on");
		            	result_str = "OK";
				    	return true;
					}
					else {
				    	Message *msg;
						if (m->getCurrent().getName() == "on") 
							msg = new Message("turnOff");
						else
							msg = new Message("turnOn");
				    	m->send(msg, m);
		            	result_str = "OK";
				    	return true;
					}
				}
			}
			else {
				error_str = "Usage: toggle device_name";
				return false;
		    }
				
            Output *device = dynamic_cast<Output *>(lookup_device(params[1]));
            if (device) {
                if (device->isOn()) device->turnOff();
                else if (device->isOff()) device->turnOn();
				else {
					error_str = "device is neither on nor off\n";
					return false;
				}
                result_str = "OK";
                return true;
            }
            else {
                error_str = "Unknown device";
                return false;
            }
        }
		else {
			error_str = "Unknown device";
			return false;
		}
	}
};

struct IODCommandProperty : public IODCommand {
    bool run(std::vector<std::string> &params) {
        if (params.size() == 4) {
			DBG_MSG << "setting property " << params[1] << "." << params[2] << " to " << params[3] << "\n";
		    MachineInstance *m = MachineInstance::find(params[1].c_str());
		    if (m) {
				long x;
				char *p;
				x = strtol(params[3].c_str(), &p, 0);
				if (*p == 0)
					m->setValue(params[2], x);
				else
					m->setValue(params[2], params[3].c_str());

                result_str = "OK";
                return true;
			}
	        else {
	            error_str = "Unknown device";
	            return false;
	        }
		}
		else {
			error_str = "Usage: PROPERTY property_name value";
			return false;
		}
	}
};

struct IODCommandList : public IODCommand {
    bool run(std::vector<std::string> &params) {
        std::ostringstream ss;
        std::map<std::string, MachineInstance*>::const_iterator iter = machines.begin();
        while (iter != machines.end()) {
            MachineInstance *m = (*iter).second;
            ss << (m->getName()) << " " << m->_type;
            if (m->_type == "POINT") ss << " " << m->properties.lookup("tab");
            ss << "\n";
            iter++;
        }
        result_str = strdup(ss.str().c_str());
        return true;
    }
};

static std::set<std::string>no_display;

cJSON *printMachineInstanceToJSON(MachineInstance *m, std::string prefix = "") {
    cJSON *node = cJSON_CreateObject();
    std::string name_str = m->getName();
    if (prefix.length()!=0) {
        std::stringstream ss;
        ss << prefix << '-' << m->getName()<< std::flush;
        name_str = ss.str();
    }
    cJSON_AddStringToObject(node, "name", name_str.c_str());
    cJSON_AddStringToObject(node, "class", m->_type.c_str());
    SymbolTableConstIterator st_iter = m->properties.begin();
    while (st_iter != m->properties.end()) {
	    std::pair<std::string, Value> item(*st_iter++);
	    if (item.second.kind == Value::t_integer)
	        cJSON_AddNumberToObject(node, item.first.c_str(), item.second.iValue);
	    else
	        cJSON_AddStringToObject(node, item.first.c_str(), item.second.asString().c_str());
	        //std::cout << item.first << ": " <<  item.second.asString() << "\n";
    }
	Action *action;
	if ( (action = m->executingCommand()))  {
		std::stringstream ss;
		ss << *action;
        cJSON_AddStringToObject(node, "executing", ss.str().c_str());
	}

	if (m->enabled()) 
		cJSON_AddTrueToObject(node, "enabled");
	else 
		cJSON_AddFalseToObject(node, "enabled");
    if (!m->io_interface) {
        cJSON_AddStringToObject(node, "state", m->getCurrentStateString());
    }
    else {
           IOComponent *device = lookup_device(m->getName().c_str());
           if (device) {
               const char *state = device->getStateString();
               cJSON_AddStringToObject(node, "state", state);
           }
    }
    if (m->commands.size()) {
		std::stringstream cmds;
		std::pair<std::string, MachineCommand*> cmd;
		const char *delim = "";
		BOOST_FOREACH(cmd, m->commands) {
			cmds << delim << cmd.first;
			delim = ",";
		}
		cmds << std::flush;
		cJSON_AddStringToObject(node, "commands", cmds.str().c_str());
    }
/*
    if (m->receives_functions.size()) {
		std::stringstream cmds;
		std::pair<Message, MachineCommand*> rcv;
		const char *delim = "";
		BOOST_FOREACH(rcv, m->receives_functions) {
			cmds << delim << rcv.first;
			delim = ",";
		}
		cmds << std::flush;
		cJSON_AddStringToObject(node, "receives", cmds.str().c_str());
    }
*/
	if (m->properties.size()) {
		std::stringstream props;
		SymbolTableConstIterator st_iter = m->properties.begin();
		int count = 0;
		const char *delim="";
		while (st_iter != m->properties.end()) {
			std::pair<std::string, Value> prop = *st_iter++;
			if (no_display.count(prop.first)) continue;
			props << delim << prop.first;
			delim = ",";
			++count;
		}
		if ( (action = m->executingCommand()))  {
			props << delim << "executing";
		}
		props << std::flush;
		if (count) cJSON_AddStringToObject(node, "display", props.str().c_str());
	}
    return node;
}


struct IODCommandListJSON : public IODCommand {
    bool run(std::vector<std::string> &params) {
        cJSON *root = cJSON_CreateArray();
		Value tab("");
		bool limited = false;
		if (params.size() > 2) {
			tab = params[2];
			limited = true;
		}
        std::map<std::string, MachineInstance*>::const_iterator iter = machines.begin();
        while (iter != machines.end()) {
            MachineInstance *m = (*iter++).second;
			if (!limited || (limited && m->properties.lookup("tab") == tab) ) {
   		        cJSON_AddItemToArray(root, printMachineInstanceToJSON(m));
		        BOOST_FOREACH(Parameter p, m->locals) {
    	       		cJSON_AddItemToArray(root, printMachineInstanceToJSON(p.machine, m->getName()));
	        	}
			}
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


/*
	send a message. The message may be in one of the forms: 
		machine-object.command or
		object.command
*/
struct IODCommandSend : public IODCommand {
    bool run(std::vector<std::string> &params) {
		if (params.size() != 2) {
			error_str = "Usage: SEND command";
			return false;
		}
		MachineInstance *m = 0;
		std::string machine_name(params[1]);
		std::string command(params[1]);
		if (machine_name.find('-') != std::string::npos) {
			machine_name.erase(machine_name.find('-'));
			command = params[1].substr(params[1].find('-')+1);
 			if (command.find('.')) {
				// another level of indirection
				std::string target = command;
				target.erase(target.find('.'));
				command = params[1].substr(params[1].find('.')+1);
				MachineInstance *owner = MachineInstance::find(machine_name.c_str());
				if (!owner) {
					error_str = "could not find machine";
					return false;
				}
				m = owner->lookup(target); // find the actual device
 			}
			else
				m = MachineInstance::find(machine_name.c_str());
		}
		else if (machine_name.find('.') != std::string::npos) {
			machine_name.erase(machine_name.find('.'));
			command = params[1].substr(params[1].find('.')+1);
			m = MachineInstance::find(machine_name.c_str());
		}
        std::stringstream ss;
		if (m) {
			m->send( new Message(strdup(command.c_str())), m, false);
			ss << command << " sent to " << m->getName() << std::flush;
			result_str = strdup(ss.str().c_str());
			return true;
		}
		else {
			ss << "Could not find machine "<<machine_name<<" for command " << command << std::flush;
		}
        error_str = strdup(ss.str().c_str());
        return false;
    }
};

struct IODCommandQuit : public IODCommand {
    bool run(std::vector<std::string> &params) {
	program_done = true;
        std::stringstream ss;
        ss << "quitting ";
        std::ostream_iterator<std::string> oi(ss, " ");
        ss << std::flush;
        result_str = strdup(ss.str().c_str());
        return true;
    }
};

struct IODCommandHelp : public IODCommand {
    bool run(std::vector<std::string> &params) {
        std::stringstream ss;
        ss 
		 << "Commands: \n"
		 << "DEBUG machine on|off"
		 << "DEBUG debug_group on|off"
		 << "DISABLE machine_name\n"
		 << "EC command\n"
		 << "ENABLE machine_name\n"
		 << "GET machine_name\n"
		 << "LIST JSON\n"
		 << "LIST\n"
		 << "MASTER\n"
		 << "MODBUS EXPORT\n"
		 << "MODBUS group address new_value\n" 
		 << "PROPERTY machine_name property new_value\n"
		 << "QUIT\n"
		 << "RESUME machine_name\n"
		 << "SEND command\n"
		 << "SET machine_name TO state_name\n"
		 << "SLAVES\n"
		 << "TOGGLE output_name\n"
		;
		std::string s = ss.str();
        result_str = strdup(s.c_str());
        return true;
    }
};

struct IODCommandDebugShow : public IODCommand {
    bool run(std::vector<std::string> &params) {
		std::stringstream ss;
		ss << "Debug status: \n" << *LogState::instance() << "\n" << std::flush;
		std::string s = ss.str();
		result_str = strdup(s.c_str());
		return true;
	}
};

struct IODCommandDebug : public IODCommand {
    bool run(std::vector<std::string> &params) {
		if (params.size() != 3) {
			std::stringstream ss;
			ss << "usage: DEBUG debug_group on|off\n\nDebug groups: \n" << *LogState::instance() << std::flush;
			std::string s = ss.str();
			error_str = strdup(s.c_str());
			return false;
		}
		int group = LogState::instance()->lookup(params[1]);
		if (group == 0) {
			std::map<std::string, MachineInstance *>::iterator found = machines.find(params[1]);
			if (found != machines.end()) {
				MachineInstance *mi = (*found).second;
				if (params[2] == "on" || params[2] == "ON") {
					mi->setDebug(true);
				}
				else {
					mi->setDebug(false);
				}
			}
			else {
				error_str = strdup("Unknown debug group");
				return false;
			}
		}
		if (params[2] == "on" || params[2] == "ON") {
			LogState::instance()->insert(group);
			if (params[1] == "DEBUG_MODBUS") ModbusAddress::message("DEBUG ON");
		}
		else if (params[2] == "off" || params[2] == "OFF") {
			LogState::instance()->erase(group);
			if (params[1] == "DEBUG_MODBUS") ModbusAddress::message("DEBUG OFF");
		}
		else {
			error_str = strdup("Please use 'on' or 'off' in all upper- or lowercase");
			return false;
		}
		result_str = strdup("OK");
		return true;
	}
};

struct IODCommandModbus : public IODCommand {
    bool run(std::vector<std::string> &params) {
		if (params.size() != 4) {
			error_str = strdup("Usage: MODBUS group address value");
			return false;
		}
		int group, address, val;
		char *end;
		group = strtol(params[1].c_str(), &end, 0);
		if (*end) {
			error_str = strdup("Modbus: group number should be from 1..4");
			return false;
		}
		address = strtol(params[2].c_str(), &end, 0);
		if (*end) {
			error_str = strdup("Modbus: address should be from 0..65535");
			return false;
		}
		val = strtol(params[3].c_str(), &end, 0);
		if (*end) {
			std::stringstream ss;
			ss  << "Modbus: value (" << params[3] << ") was expected to be a number from 0..65535\n";
			std::string s = ss.str();
			error_str = strdup(s.c_str());
			return false;
		}
		ModbusAddress found = ModbusAddress::lookup(group, address);
		if (found.getGroup() != ModbusAddress::none) {
			if (found.getOwner()) {
				// the address found will refer to the base address, so we provide the actual offset
				assert(address == found.getAddress());
				found.getOwner()->modbusUpdated(found, address - found.getAddress(), val);
			}
			else {
				DBG_MODBUS << "no owner for Modbus address " << found << "\n";
				error_str = strdup("Modbus: ignoring unregistered address\n");
			}
		}
		else {
			std::stringstream ss;
			ss << "failed to find Modbus Address matching group: " << group << ", address: " << address;
			error_str = strdup(ss.str().c_str()); 
			return false;
		}
		result_str = strdup("OK");
		return true;
	}
};

struct IODCommandModbusExport : public IODCommand {
    bool run(std::vector<std::string> &params) {

		const char *file_name = modbus_map();
		const char *backup_file_name = "modbus_mappings.bak";
		if (rename(file_name, backup_file_name)) {
			std::cerr << strerror(errno) << "\n";
		}
		std::list<MachineInstance*>::iterator m_iter = MachineInstance::begin();
		std::ofstream out(file_name);
		if (!out) {
			error_str = strdup("not able to open mapping file for write");
			return false;
		}
		while (m_iter != MachineInstance::end()) {
			(*m_iter)->exportModbusMapping(out);
			m_iter++;
		}
		out.close();
		result_str = strdup("OK");
		return true;
	}
};

struct IODCommandModbusRefresh : public IODCommand {
    bool run(std::vector<std::string> &params) {
		std::list<MachineInstance*>::iterator m_iter = MachineInstance::begin();
		std::stringstream out;
		while (m_iter != MachineInstance::end()) {
			(*m_iter)->refreshModbus(out);
			m_iter++;
		}
		std::string s(out.str());
		result_str = strdup(s.c_str());
		return true;
	}
};

struct IODCommandUnknown : public IODCommand {
    bool run(std::vector<std::string> &params) {
        std::stringstream ss;
        ss << "Unknown command: ";
        std::ostream_iterator<std::string> oi(ss, " ");
        ss << std::flush;
        error_str = strdup(ss.str().c_str());
        return false;
    }
};

static void sendMessage(zmq::socket_t &socket, const char *message) {
    const char *msg = (message) ? message : "";
    size_t len = strlen(msg);
    zmq::message_t reply (len);
    memcpy ((void *) reply.data (), msg, len);
    socket.send (reply);
}

struct IODCommandThread {
    void operator()() {
		std::cout << "------------------ Command Thread Started -----------------\n";
        zmq::context_t context (1);
        zmq::socket_t socket (context, ZMQ_REP);
        socket.bind ("tcp://*:5555");
        IODCommand *command = 0;
        
        while (!done) {
	        boost::mutex::scoped_lock lock(q_mutex);
            zmq::message_t request;
            try {
	            //  Wait for next request from client
	            socket.recv (&request);
				if (!machine_is_ready) {
					sendMessage(socket, "Ignored during startup");
				}
	            size_t size = request.size();
	            char *data = (char *)malloc(size+1);
	            memcpy(data, request.data(), size);
	            data[size] = 0;
	            //std::cout << "Command thread received " << data << std::endl;
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
				if (params.empty()) {
					sendMessage(socket, "Empty message received\n");
					goto cleanup;
				}
	            ds = params[0];
				{

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
					else if (count == 2 && ds == "DEBUG" && params[1] == "SHOW") {
						command = new IODCommandDebugShow;
					}
					else if (count == 3 && ds == "DEBUG") {
						command = new IODCommandDebug;
					}
		            else if (count == 2 && ds == "TOGGLE") {
		                command = new IODCommandToggle;
		            }
		            else if (count == 1 && ds == "LIST") {
		                command = new IODCommandList;
		            }
		            else if (count == 2 && ds == "SEND") {
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
					else if (ds == "PROPERTY") {
						command = new IODCommandProperty;
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
					else if (ds == "HELP") {
						command = new IODCommandHelp;
					}
		            else {
		                command = new IODCommandUnknown;
		            }
				}
	            if ((*command)(params)) {
	                sendMessage(socket, command->result());
				}
	            else {
					NB_MSG << command->error() << "\n";
	                sendMessage(socket, command->error());
				}				
	            delete command;

				cleanup:

	            free(data);
            }
            catch (std::exception e) {
                //std::cout << e.what() << "\n";
				usleep(10000);
               //exit(1);
            }
        } 
		socket.close();
    }
    IODCommandThread() : done(false) {
        
    }
    void stop() { done = true; }
    bool done;
};

void usage(int argc, char const *argv[])
{
    fprintf(stderr, "Usage: %s [-v] [-l logfilename] [-i persistent_store] [-c debug_config_file] [-m modbus_mapping] [-g graph_output] [-s maxlogfilesize] \n", argv[0]);
}

int loadConfig(int argc, char const *argv[]) {
    struct timeval now_tv;
    const char *logfilename = NULL;
    long maxlogsize = 20000;
    int i;
    int opened_file = 0;
    tzset(); /* this initialises the tz info required by ctime().  */
    gettimeofday(&now_tv, NULL);
    srandom(now_tv.tv_usec);
	boost::mutex::scoped_lock lock(q_mutex);

    
    std::list<std::string> files;
    /* check for commandline options, later we process config files in the order they are named */
    i=1;
    while ( i<argc)
    {
        if (strcmp(argv[i], "-v") == 0)
            set_verbose(1);
		else if (strcmp(argv[i], "-t") == 0)
			set_test_only(1);
        else if (strcmp(argv[i], "-l") == 0 && i < argc-1)
            logfilename = argv[++i];
        else if (strcmp(argv[i], "-s") == 0 && i < argc-1)
            maxlogsize = strtol(argv[++i], NULL, 10);
		else if (strcmp(argv[i], "-i") == 0 && i < argc-1) { // initialise from persistent store
			set_persistent_store(argv[++i]);
		}
		else if (strcmp(argv[i], "-c") == 0 && i < argc-1) { // debug config file
			set_debug_config(argv[++i]);
		}
		else if (strcmp(argv[i], "-m") == 0 && i < argc-1) { // modbus mapping file
			set_modbus_map(argv[++i]);
		}
		else if (strcmp(argv[i], "-g") == 0 && i < argc-1) { // dependency graph file
			set_dependency_graph(argv[++i]);
		}
        else if (*(argv[i]) == '-' && strlen(argv[i]) > 1)
        {
            usage(argc, argv);
            exit(2);
        }
        else {
            struct stat file_stat;
            int err = stat(argv[i], &file_stat);
            if (err == -1) {
                std::cerr << "Error: " << strerror(errno) << " checking file type for " << argv[i] << "\n";
            }
            else if (file_stat.st_mode & S_IFDIR){
                list_directory(argv[i], files);
            }
            else {
                files.push_back(argv[i]);
            }
        }
        i++;
    }
    
    if (logfilename)
    {
        int ferr;
        ferr = fflush(stdout);
        ferr = fflush(stderr);
        stdout = freopen(logfilename, "w+", stdout);
        ferr = dup2(1, 2);
    }

	if (!modbus_map()) set_modbus_map("modbus_mappings.txt");
	if (!debug_config()) set_debug_config("iod.conf");
	
	// load preset modbus mappings
	std::ifstream modbus_mappings_file(modbus_map());
	if (!modbus_mappings_file) {
		std::cout << "No preset modbus mapping\n";
	}
	else {
		char buf[200];
		int lineno = 0;
		int errors = 0;
		while (modbus_mappings_file.getline(buf, 200,'\n')) {
			++lineno;
			std::stringstream line(buf);
			std::cout << buf << "\n";
			std::string group_addr, group, addr, name, type;
			line >> group_addr ;
			size_t pos = group_addr.find(':');
			if (pos != std::string::npos) {
				group = group_addr;
				line >> name >> type;
				group.erase(pos);
				addr = group_addr;
				addr = group_addr.substr(pos+1);
				if (ModbusAddress::preset_modbus_mapping.count(name) == 0) {
					int group_num, addr_num;
					char *end = 0;
					group_num = strtol(group.c_str(), &end, 10);
					if (*end) {
						std::cerr << "error loading modbus mappings from " << modbus_map() 
						<< " at line " << lineno << " " << *end << "not expected\n";
						++errors;
						continue;
					}
					addr_num = strtol(addr.c_str(), &end, 10);
					if (*end) {
						std::cerr << "error loading modbus mappings from " << modbus_map() 
						<< " at line " << lineno << " " << *end << "not expected\n";
						++errors;
						continue;
					}
					int len = 1;
					if (type == "Signed_int_32") len = 2;
					DBG_MODBUS << "Loaded modbus mapping " << group_num << " " << addr << " " << len << "\n";
					ModbusAddressDetails details(group_num, addr_num, len);
					ModbusAddress::preset_modbus_mapping[name] = details;
				}
			}
			else {
				std::cerr << "error loading modbus mappings from " << modbus_map() << " at line " << lineno << " missing ':' in " << group_addr << "\n";
				++errors;
			}
		}
		if (errors) {
			std::cerr << errors << " errors. aborting.\n";
			exit(1);
		}
	}
    
    std::cout << (argc-1) << " arguments\n";
    
    /* load configuration from files named on the commandline */
    std::list<std::string>::iterator f_iter = files.begin();
    while (f_iter != files.end())
    {
        const char *filename = (*f_iter).c_str();
        if (filename[0] != '-')
        {
            opened_file = 1;
            yyin = fopen(filename, "r");
            if (yyin)
            {
                std::cout << "\nProcessing file: " << filename << "\n";
                yylineno = 1;
                yycharno = 1;
                yyfilename = filename;
                yyparse();
                fclose(yyin);
            }
            else
            {
				std::stringstream ss;
                ss << "## - Error: failed to load config: " << filename;
				error_messages.push_back(ss.str());
                ++num_errors;
            }
        }
        else if (strlen(filename) == 1) /* '-' means stdin */
        {
            opened_file = 1;
            std::cout << "\nProcessing stdin\n";
			yyfilename = "stdin";
            yyin = stdin;
            yylineno = 1;
            yycharno = 1;
            yyparse();
        }
        f_iter++;
    }
    
    /* if we weren't given a config file to use, read config data from stdin */
    if (!opened_file)
        yyparse();
    
    if (num_errors > 0)
    {
		BOOST_FOREACH(std::string &error, error_messages) {
			std::cerr << error << "\n";
		}
        printf("Errors detected. Aborting\n");
        exit(2);
    }

    MachineClass *point_class = new MachineClass("POINT");
    point_class->parameters.push_back(Parameter("module"));
    point_class->parameters.push_back(Parameter("offset"));
    point_class->states.push_back("on");
    point_class->states.push_back("off");
	point_class->default_state = State("off");
	point_class->initial_state = State("off");
	point_class->disableAutomaticStateChanges();

    MachineClass *module_class = new MachineClass("MODULE");
    //module_class->parameters.push_back(Parameter("position")); // not using this parameters
	module_class->disableAutomaticStateChanges();

	MachineClass *cond = new MachineClass("CONDITION");
	cond->states.push_back("true");
	cond->states.push_back("false");
	cond->default_state = State("false");

	MachineClass *flag = new MachineClass("FLAG");
	flag->states.push_back("on");
	flag->states.push_back("off");
	flag->default_state = State("off");
	flag->initial_state = State("off");
	flag->disableAutomaticStateChanges();
//	flag->transitions.push_back(Transition(State("on"),State("off"),Message("turnOff")));
//	flag->transitions.push_back(Transition(State("off"),State("on"),Message("turnOn")));
	
	MachineClass *mc_variable = new MachineClass("VARIABLE");
	mc_variable->states.push_back("ready");
	mc_variable->initial_state = State("ready");
	mc_variable->disableAutomaticStateChanges();
	mc_variable->parameters.push_back(Parameter("VAL_PARAM1"));
	mc_variable->options["VALUE"] = "VAL_PARAM1";
	//MachineCommandTemplate *mc_cmd = new MachineCommandTemplate("SYMBOL", "SYMBOL");
	//mc_cmd->setActionTemplate(new PredicateActionTemplate(
	//	new Predicate(new Predicate("VALUE"), opAssign, new Predicate("VAL_PARAM1"))));
	//mc_variable->receives[Message(strdup("INIT_enter"))] = mc_cmd;
	mc_variable->properties.add("PERSISTENT", Value("true", Value::t_string), SymbolTable::ST_REPLACE);
	
	MachineClass *mc_constant = new MachineClass("CONSTANT");
	mc_constant->states.push_back("ready");
	mc_constant->initial_state = State("ready");
	mc_constant->disableAutomaticStateChanges();
	mc_constant->parameters.push_back(Parameter("VAL_PARAM1"));
	mc_constant->options["VALUE"] = "VAL_PARAM1";
	//mc_cmd = new MachineCommandTemplate("SYMBOL", "SYMBOL");
	//mc_cmd->setActionTemplate(new PredicateActionTemplate(new Predicate(new Predicate("VALUE"), opAssign, new Predicate("VAL_PARAM1"))));
	//mc_constant->receives[Message(strdup("INIT_enter"))] = mc_cmd;
	mc_constant->properties.add("PERSISTENT", Value("true", Value::t_string), SymbolTable::ST_REPLACE);

/*
	flag->stable_states.push_back(StableState("on", new Predicate(new Predicate("SELF"), opEQ, new Predicate("on"))));
	flag->stable_states.push_back(StableState("off", new Predicate(new Predicate("SELF"), opEQ, new Predicate("off"))));
	
	flag->transitions.push_back(Transition(State("off"), State("on"), Message("turnOn")));
	flag->transitions.push_back(Transition(State("on"), State("off"), Message("turnOff")));

	MachineCommandTemplate *mct = new MachineCommandTemplate("turnOn", "turnOn");
	mct->setActionTemplate(new MoveStateActionTemplate("SELF", "on"));
	flag->commands["turnOn"] = mct;
	mct = new MachineCommandTemplate("turnOff", "turnOff");
	mct->setActionTemplate(new MoveStateActionTemplate("SELF", "off"));
	flag->commands["turnOff"] = mct;
*/
    std::map<std::string, MachineInstance*> machine_instances;

    std::map<std::string, MachineInstance*>::const_iterator iter = machines.begin();
    while (iter != machines.end()) {
        MachineInstance *m = (*iter).second;
	    if (!m) {
			std::stringstream ss;
			ss << "## - Warning: machine table entry " << (*iter).first << " has no machine";
			error_messages.push_back(ss.str());
		}
	
        machine_instances[m->getName()] = m;
        iter++;
    }

    // display machine classes and build a map of names to classes
    // also move the DEFAULT stable state to the end
    std::cout << "\nMachine Classes\n";
    std::ostream_iterator<Parameter> out(std::cout, ", ");
    BOOST_FOREACH(MachineClass*mc, MachineClass::all_machine_classes) {
        std::cout << mc->name << " (";
        std::copy(mc->parameters.begin(), mc->parameters.end(), out);
        std::cout << ")\n";
        if (mc->stable_states.size()) {
            std::ostream_iterator<StableState> ss_out(std::cout, ", ");

			unsigned int n = mc->stable_states.size();
			unsigned int i;
			// find the default state and move it to the end of the stable state tests
			for (i=0; i < n-1; ++i) {
				if (mc->stable_states[i].condition.predicate && mc->stable_states[i].condition.predicate->priority == 1) break;
			}
			if (i < n-1) {
				StableState tmp = mc->stable_states[i];
				while (i < n-1) {
					mc->stable_states[i] = mc->stable_states[i+1];
					++i;
				}
				mc->stable_states[n-1] = tmp;
			}

            std::cout << "stable states: ";
            std::copy(mc->stable_states.begin(), mc->stable_states.end(), ss_out);
            std::cout <<"\n";
        }
        machine_classes[mc->name] = mc;
    }
	// setup references for each global
    BOOST_FOREACH(MachineClass*mc, MachineClass::all_machine_classes) {
		if (!mc->global_references.empty()) {
			std::pair<std::string, MachineInstance *>node;
			BOOST_FOREACH(node, mc->global_references) {
				if (machines.count(node.first) == 0) {
					std::stringstream ss;
					ss << "## - Warning: Cannot find a global variable named " << node.first << "\n";
					error_messages.push_back(ss.str());
					++num_errors;
				}
				else
					mc->global_references[node.first] = machines[node.first];
			}
		}
    }

    // display all machine instances and link classes to their instances
    std::cout << "\nDefinitions\n";
    std::list<MachineInstance *>::iterator m_iter = MachineInstance::begin();
    while (m_iter != MachineInstance::end()) {
        MachineInstance *m = *m_iter++;
        std::map<std::string, MachineClass*>::iterator c_iter = machine_classes.find(m->_type);
        if (c_iter == machine_classes.end()) {
			std::stringstream ss;
            ss <<"## - Error: class " << m->_type << " not found for machine " << m->getName() << "\n";
			error_messages.push_back(ss.str());
			++num_errors;
		}
        else if ((*c_iter).second) {
			MachineClass *machine_class = (*c_iter).second;
            m->setStateMachine(machine_class);
        }
        else {
            std::cout << "Error: no state machine defined for instance " << (*c_iter).first << "\n";
        }
    }
    
    // stitch the definitions together
    std::pair<std::string, MachineInstance*> node;
    BOOST_FOREACH(node, machines) {
        MachineInstance *mi = node.second;
        std::cout << "Machine " << mi->getName() << " has " << mi->parameters.size() << " parameters\n";

		if (mi->getStateMachine() && mi->parameters.size() != mi->getStateMachine()->parameters.size()) {
			std::stringstream ss; 
			ss << "## - Error: Machine " << mi->getStateMachine()->name << " requires " 
				<< mi->getStateMachine()->parameters.size()
				<< " parameters but instance " << mi->getName() << " has " << mi->parameters.size() << std::flush;
			std::string s = ss.str();
			++num_errors;
			error_messages.push_back(s);
		}

        for (unsigned int i=0; i<mi->parameters.size(); i++) {
            Value p_i = mi->parameters[i].val;
            if (p_i.kind == Value::t_symbol) {
	            //mi->lookup(mi->parameters[i].val); // TBD remove?
                std::cout << "  parameter " << i << " " << p_i.sValue << " (" << mi->parameters[i].real_name << ")\n";
				MachineInstance *found = mi->lookup(mi->parameters[i]); // uses the real_name field and caches the result
				if (found) {
                    if (mi->_type == "POINT" && i == 0 && found->_type != "MODULE") {
                        std::cout << "Error: in the definition of " << mi->getName() << ", " <<
                            p_i.sValue << " has type " << found->_type << " but should be MODULE\n";
                    }
                    else { // TBD why the else clause?
                        mi->parameters[i].machine = found;
					}
                }
                else {
					std::stringstream ss;
                    ss << "Warning: no instance " << p_i.sValue 
						<< " (" << mi->parameters[i].real_name << ")"
						<< " found for " << mi->getName();
					error_messages.push_back(ss.str());
				}
            }
		    else
                std::cout << "  parameter " << i << " " << p_i << "\n";

        }
		// make sure that local machines have correct links to their 
		// parameters
		DBG_MSG << "fixing parameter references for locals in " << mi->getName() << "\n";
		for (unsigned int i=0; i<mi->locals.size(); ++i) {
			DBG_MSG << "   " << i << ": " << mi->locals[i].val << "\n";
			for (unsigned int j=0; j<mi->locals[i].machine->parameters.size(); ++j) {
				MachineInstance *m = mi->locals[i].machine;
				Parameter p = m->parameters[j];
				if (p.val.kind == Value::t_symbol) {
					DBG_MSG << "      " << j << ": " << p.val << "\n";
					m->parameters[j].machine = mi->lookup(m->parameters[j]);
					if (m->parameters[j].machine) {
						m->parameters[j].machine->addDependancy(m);
						m->listenTo(m->parameters[j].machine);
						DBG_MSG << " linked " << m->parameters[j].val << " to local " << m->getName() << "\n";
					}
					else {
						std::stringstream ss;
						ss << "## - Error: local parameter " << m->getName() << " for machine " << mi->getName() << " sends an unknown parameter " << p.val << "\n";
						error_messages.push_back(ss.str());
						++num_errors;
					}
				}
			}
		}
	}	

	// setup triggered actions for the stable states for each machine
    //BOOST_FOREACH(node, machines) {
	std::list<MachineInstance*>::iterator am_iter = MachineInstance::begin();
	am_iter = MachineInstance::begin();
	while (am_iter != MachineInstance::end()) {
        MachineInstance *mi = *am_iter++;
		if (mi->getStateMachine()) {
			BOOST_FOREACH(StableState &ss, mi->stable_states) {
				ss.condition(mi); // evaluate the conditions to prep caches and dependancies
				for (unsigned int i=0; i<mi->locals.size(); ++i) {
					MachineInstance *local = mi->locals[i].machine;
					if (local){			
						BOOST_FOREACH(StableState &lmss, local->stable_states) {
							lmss.condition(local);
							if (lmss.condition.predicate && lmss.condition.predicate->error()) {
								//++num_errors; // dont' abort at this point
								error_messages.push_back(lmss.condition.predicate->errorString());
							}
						}
					}
				}
				if (ss.condition.predicate->usesTimer(ss.timer_val) ) 
					ss.uses_timer = true;
				bool subcond_uses_timer = false;
				if (ss.subcondition_handlers) {
					BOOST_FOREACH(ConditionHandler&ch, *ss.subcondition_handlers) {
						if (ch.condition.predicate && ch.condition.predicate->usesTimer(ch.timer_val))
							ch.uses_timer = true;
						else 
							ch.uses_timer = false;
					}
				}
				if (subcond_uses_timer || ss.uses_timer) {
					mi->uses_timer = true;
					continue;
				}
			}
		}
	}

	// make sure that references to globals are configured with dependencies
	am_iter = MachineInstance::begin();
	while (am_iter != MachineInstance::end()) {
		MachineInstance *mi = *am_iter++;
		if (mi->getStateMachine()) {
			std::pair<std::string, MachineInstance *>node;
			BOOST_FOREACH(node, mi->getStateMachine()->global_references) {
				if (node.second) {
					node.second->addDependancy(mi);
					mi->listenTo(node.second);
				}
				else {
					DBG_MSG << "Warning: " << node.first << " has no machine when settingup globals\n";
				}
			}
		}
	}
	
	// add receives_function entries for each machine to ensure real_name messages are captured
	m_iter = MachineInstance::begin();
	while (m_iter != MachineInstance::end()) {
		MachineInstance *mi = *m_iter++;

		std::list< std::pair<Message, MachineCommand*> >to_add;
		std::pair<Message, MachineCommand*> rcv;
		BOOST_FOREACH(rcv, mi->receives_functions) {
			if (rcv.first.getText().find('.') != std::string::npos) to_add.push_back(rcv);
		}

		BOOST_FOREACH(rcv, to_add) {
			std::string machine(rcv.first.getText());
			std::string event(machine);
			machine.erase(machine.find('.'));
			event = event.substr(event.find('.')+1);
			MachineInstance *source = mi->lookup(machine);
			if (source) {
				DBG_MSG << "Checking whether to duplicate " << rcv.first.getText() << " in " << source->getName() << " (" << machine << ")" << "\n";
			}
			else {
				DBG_MSG << "Unknown machine when checking to duplicate " << rcv.first.getText() << "\n";
			}
			
			if (source && source->getName() != machine) {
				DBG_MSG << "duplicating receive function for " << mi->getName() << " from " << source->getName() << " (" << machine << ")" << "\n";
				event = source->getName() + "." + event;
				mi->receives_functions[Message(event.c_str())] = rcv.second;
			}
		}
	}
	
	// reorder the list of machines in reverse order of dependence
	MachineInstance::sort();

	
	// display errors and warnings
	BOOST_FOREACH(std::string &error, error_messages) {
		std::cerr << error << "\n";
	}
	// abort if there were errors
    if (num_errors > 0)
    {
        printf("Errors detected. Aborting\n");
        exit(2);
    }

	std::cout << " Configuration loaded. " << MachineInstance::countAutomaticMachines() << " automatic machines\n";
	//MachineInstance::displayAutomaticMachines();
    return 0;
}

void load_debug_config() {
	std::ifstream program_config(debug_config());
	if (program_config) {
		std::string debug_flag;
		while (program_config >> debug_flag) {
			if (debug_flag[0] == '#') continue;
			int dbg = LogState::instance()->lookup(debug_flag);
			if (dbg) LogState::instance()->insert(dbg);
			else if (machines.count(debug_flag)) {
				MachineInstance *mi = machines[debug_flag];
				if (mi) mi->setDebug(true);
			}
			else std::cerr << "Warning: unrecognised DEBUG Flag " << debug_flag << "\n";
		}
	}
}

int main (int argc, char const *argv[])
{
	unsigned int user_alarms = 0;
	Logger::instance();
	ControlSystemMachine machine;

    Logger::instance()->setLevel(Logger::Debug);
	LogState::instance()->insert(DebugExtra::instance()->DEBUG_PARSER);

	load_debug_config();

#if 0
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_PREDICATES);
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_INITIALISATION);
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_MESSAGING);
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_ACTIONS);
	//std::cout << DebugExtra::instance()->DEBUG_PREDICATES << "\n";
	//assert (!LogState::instance()->includes(DebugExtra::instance()->DEBUG_PREDICATES));
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_SCHEDULER);
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_PROPERTIES);
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_MESSAGING);
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_STATECHANGES);
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_AUTOSTATES);
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_MODBUS);
#endif
	
	no_display.insert("tab");
	no_display.insert("type");
	no_display.insert("name");
	no_display.insert("image");
	no_display.insert("class");
	no_display.insert("state");
	no_display.insert("export");
	no_display.insert("startup_enabled");
	no_display.insert("NAME");
	no_display.insert("STATE");

	statistics = new Statistics;
	int load_result = loadConfig(argc, argv);
	if (load_result)
		return load_result;
    if (dependency_graph()) {
		std::cout << "writing dependency graph to " << dependency_graph() << "\n";
        std::ofstream graph(dependency_graph());
        if (graph) {
            graph << "digraph G {\n";
            std::list<MachineInstance *>::iterator m_iter;
            m_iter = MachineInstance::begin();
            while (m_iter != MachineInstance::end()) {
                MachineInstance *mi = *m_iter++;
				if (!mi->depends.empty()) {
                	BOOST_FOREACH(MachineInstance *dep, mi->depends) {
                		graph << mi->getName() << " -> " << dep->getName() << ";\n";
					}
				}
            }
            graph << "}\n";
        }
        else {
            std::cerr << "not able to open " << dependency_graph() << " for write\n";
        }
    }

	if (test_only() ) {
		const char *backup_file_name = "modbus_mappings.bak";
		rename(modbus_map(), backup_file_name);
		// export the modbus mappings and exit
        std::list<MachineInstance*>::iterator m_iter = MachineInstance::begin();
        std::ofstream out(modbus_map());
        if (!out) {
            std::cerr << "not able to open " << modbus_map() << " for write\n";
            return false;
        }    
        while (m_iter != MachineInstance::end()) {
            (*m_iter)->exportModbusMapping(out);
            m_iter++;
        }    
        out.close();

		return load_result;
	}
	
	ECInterface::FREQUENCY=1000;

#ifndef EC_SIMULATOR
	/*std::cout << "init slaves: " << */
	collectSlaveConfig(true) /*<< "\n"*/;
	ECInterface::instance()->activate();
#endif
	ECInterface::instance()->start();

	std::list<Output *> output_list;
	{
		boost::mutex::scoped_lock lock(q_mutex);

		int remaining = machines.size();
		std::cout << remaining << " Machines\n";
		std::cout << "Linking POINTs to hardware\n";
		std::map<std::string, MachineInstance*>::const_iterator iter = machines.begin();
		while (iter != machines.end()) {
			MachineInstance *m = (*iter).second; iter++;
			--remaining;
			if (m->_type == "POINT" && m->parameters.size() > 1) {
				// points should have two parameters, the name of the io module and the bit offset
				//Parameter module = m->parameters[0];
				//Parameter offset = m->parameters[1];
				//Value params = p.val;
				//if (params.kind == Value::t_list && params.listValue.size() > 1) {
					std::string name = m->parameters[0].real_name;
					int bit_position = m->parameters[1].val.iValue;
					std::cerr << "Setting up point " << m->getName() << " " << bit_position << " on module " << name << "\n";
					MachineInstance *module_mi = MachineInstance::find(name.c_str());
					if (!module_mi) {
						std::cerr << "No machine called " << name << "\n";
						continue;
					}
					if (!module_mi->properties.exists("position")) { // module position not given
						std::cerr << "Machine " << name << " does not specify a position\n";
						continue; 
					}
					std::cerr << module_mi->properties.lookup("position").kind << "\n";
					int module_position = module_mi->properties.lookup("position").iValue;
					if (module_position == -1)  { // module position unmapped
						std::cerr << "Machine " << name << " position not mapped\n";
						continue; 
					}
				
	#ifndef EC_SIMULATOR
					// some modules have multiple io types (eg the EK1814) and therefore
					// multiple syncmasters, we number 
					// the points from 1..n but the device numbers them 1.n,1.m,..., resetting
					// the index for each sync master.
					ECModule *module = ECInterface::findModule(module_position);
					if (!module) {
						std::cerr << "No module found at position " << module_position << "\n";
						continue;
					}
				
					unsigned int sm_idx = 0;
					unsigned int bit_pos = bit_position;
					if (module->sync_count>1) {
						while (sm_idx < module->sync_count && (bit_pos+1) > module->syncs[sm_idx].n_pdos) {
							bit_pos -= module->syncs[sm_idx].n_pdos;
							++sm_idx;
						}
					}
					char *device_type;
					if (module->syncs[sm_idx].dir == EC_DIR_OUTPUT)
						device_type = strdup("Output");
					else
						device_type = strdup("Input");
#if 0
					// default to input if the point type is not specified
					char *device_type;
					if (m->properties.exists("type")) 
						device_type = strdup(m->properties.lookup("type").asString().c_str());
					else
						device_type = strdup("Input");
					//std::stringstream sstr;
#endif

					if (strcmp(device_type, "Output") == 0) {
						//sstr << m->getName() << "_OUT_" << bit_position << std::flush;
						//const char *name_str = sstr.str().c_str();
						std::cerr << "Adding new output device " << m->getName() 
							<< " sm_idx: " << sm_idx << " bit_pos: " << bit_pos 
							<< " offset: " << module->offsets[sm_idx] <<  "\n";
						IOComponent::add_io_entry(m->getName().c_str(), module->offsets[sm_idx], bit_pos);
						Output *o = new Output(module->offsets[sm_idx], bit_pos);
						output_list.push_back(o);
						devices[m->getName().c_str()] = o;
						o->setName(m->getName().c_str());
						m->io_interface = o;
						o->addDependent(m);
					}
					else {
						//sstr << m->getName() << "_IN_" << bit_position << std::flush;
						//const char *name_str = sstr.str().c_str();
						std::cerr << "Adding new input device " << m->getName().c_str() 
							<< " sm_idx: " << sm_idx << " bit_pos: " << bit_pos 
							<< " offset: " << module->offsets[sm_idx] <<  "\n";
						IOComponent::add_io_entry(m->getName().c_str(), module->offsets[sm_idx], bit_pos);
						Input *in = new Input(module->offsets[sm_idx], bit_pos);
						devices[m->getName().c_str()] = in;
						in->setName(m->getName().c_str());
						m->io_interface = in;
						in->addDependent(m);
					}
					free(device_type);
	#endif
				}
				else {
					if (m->_type != "POINT")
						DBG_MSG << "Skipping " << m->_type << " " << m->getName() << " (not a POINT)\n";
					else  
						DBG_MSG << "Skipping " << m->_type << " " << m->getName() << " (no parameters)\n";
				}
			}
			assert(remaining==0);
		}
		MachineInstance::displayAll();
#ifdef EC_SIMULATOR
		wiring["EL2008_OUT_3"].push_back("EL1008_IN_1");
#endif
#if 0
		std::list<Output *>::const_iterator o_iter = output_list.begin();
		while (o_iter != output_list.end()) {
			Output *o = *o_iter++;
			o->turnOff();
		}
#endif

	std::cout << "-------- Initialising ---------\n";	
	
	std::list<MachineInstance *>::iterator m_iter;

	if (persistent_store()) {
		// load the store into a map
		typedef std::pair<std::string, Value> PropertyPair;
		std::map<std::string, std::list<PropertyPair> >init_values;
		ifstream store(persistent_store());
		char buf[200];
		while (store.getline(buf, 200, '\n')) {
			std::istringstream in(buf);
			std::string name, property, value_str;
			in >> name >> property >> value_str;
			init_values[name].push_back(make_pair(property, value_str.c_str()));
			std::cout << name <<"." << property << ":" << value_str << "\n";
		}

		// enable all persistent variables and set their value to the 
		// value in the map.
		m_iter = MachineInstance::begin();
	    while (m_iter != MachineInstance::end()) {
			MachineInstance *m = *m_iter++;
			if (m && (m->_type == "CONSTANT" || m->getValue("PERSISTENT") == "true")) {
				std::string name("");
				if (m->owner) name += m->owner->getName() + ".";
				name += m->getName();
				m->enable();
				if (init_values.count(name)) {
					std::list< PropertyPair > &list = init_values[name];
					PropertyPair node;
					BOOST_FOREACH(node, list) {
						long v;
						DBG_INITIALISATION << name << "initialising " << node.first << " to " << node.second << "\n";
						if (node.second.asInteger(v))
							m->setValue(node.first, v);
						else
							m->setValue(node.first, node.second);
					}
				}
			}
		}
	}

	// prepare the list of machines that will be processed at idle time
	m_iter = MachineInstance::begin();
	while (m_iter != MachineInstance::end()) {
	    MachineInstance *mi = *m_iter++;
	    if (!mi->receives_functions.empty() || mi->commands.size()
			|| !mi->getStateMachine()->transitions.empty() 
			|| mi->isModbusExported() 
                	|| mi->uses_timer ) {
	        mi->markActive();
	        DBG_INITIALISATION << mi->getName() << " is active\n";
	    }
	    else {
	        mi->markPassive();
	        DBG_INITIALISATION << mi->getName() << " is passive\n";
	    }
	}

	// enable all other machines

	bool only_startup = machine_classes.count("STARTUP") > 0;
	m_iter = MachineInstance::begin();
	while (m_iter != MachineInstance::end()) {
		MachineInstance *m = *m_iter++;	
		Value enable = m->getValue("startup_enabled");
		if (enable == SymbolTable::Null || enable == true) {
			if (!only_startup || (only_startup && m->_type == "STARTUP") ) m->enable();
		}
		else {
			DBG_INITIALISATION << m->getName() << " is disabled at startup\n";
		}
	}


	std::cout << "-------- Starting Command Interface ---------\n";	
	IODCommandThread stateMonitor;
	boost::thread monitor(boost::ref(stateMonitor));
	
	Statistic *cycle_delay_stat = new Statistic("Cycle Delay");
	Statistic::add(cycle_delay_stat);
	long delta, delta2;

	// Inform the modbus interface we have started
	load_debug_config();
	ModbusAddress::message("STARTUP");

	while (!program_done) {
			// Note: the use of pause here introduces a random variation of up to 500ns
			pause();
			struct timeval start_t, end_t;
			{
				//boost::mutex::scoped_lock lock(q_mutex);

				gettimeofday(&start_t, 0);
				if (machine_is_ready) {
					delta = get_diff_in_microsecs(&start_t, &end_t);
					cycle_delay_stat->add(delta);
				}
				if (ECInterface::sig_alarms != user_alarms)
				{
					ECInterface::instance()->collectState();
			
					gettimeofday(&end_t, 0);
					delta = get_diff_in_microsecs(&end_t, &start_t);
					statistics->io_scan_time.add(delta);
					delta2 = delta;
			
		#ifdef EC_SIMULATOR
			        checkInputs(); // simulated wiring between inputs and outputs
		#endif
					if (machine.connected()) {
					    //IOComponent::processAll();
			            //MachineInstance::processAll(MachineInstance::BUILTINS);

						gettimeofday(&end_t, 0);
						delta = get_diff_in_microsecs(&end_t, &start_t);
						statistics->points_processing.add(delta - delta2); delta2 = delta;

					    if (!machine_is_ready) {
							std::cout << "----------- Machine is Ready --------\n";
							machine_is_ready = true;
							BOOST_FOREACH(std::string &error, error_messages) {
								std::cerr << error << "\n";
							}
					    }
						//do {
				            MachineInstance::processAll(MachineInstance::NO_BUILTINS);
							gettimeofday(&end_t, 0);
							delta = get_diff_in_microsecs(&end_t, &start_t);
							statistics->machine_processing.add(delta - delta2); delta2 = delta;
						    Dispatcher::instance()->idle();
							gettimeofday(&end_t, 0);
							delta = get_diff_in_microsecs(&end_t, &start_t);
							statistics->dispatch_processing.add(delta - delta2); delta2 = delta;
						//} while (delta <100);
						Scheduler::instance()->idle();
						//MachineInstance::updateAllTimers(MachineInstance::NO_BUILTINS);
						MachineInstance::checkStableStates();
						gettimeofday(&end_t, 0);
						delta = get_diff_in_microsecs(&end_t, &start_t);
						statistics->auto_states.add(delta - delta2); delta2 = delta;
					}
					ECInterface::instance()->sendUpdates();
					++user_alarms;			
					if (ECInterface::sig_alarms != user_alarms)  user_alarms = ECInterface::sig_alarms-1;  // drop extra polls we missed
				}
				//else {
				//	std::cout << "wc state: " << ECInterface::domain1_state.wc_state << "\n"
				//		<< "Master link up: " << ECInterface::master_state.link_up << "\n";
				//}
				std::cout << std::flush;
			}
	}
    stateMonitor.stop();
    monitor.join();
	return 0;
}
