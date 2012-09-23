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

#include "IODCommands.h"
#include "MachineInstance.h"
#include "IOComponent.h"
#include <list>
#include "Logger.h"
#include "DebugExtra.h"
#include "cJSON.h"
#include <fstream>
#include "options.h"
#include "Statistic.h"
#include "Statistics.h"
#ifdef USE_ETHERCAT
#include <tool/MasterDevice.h>
#endif

extern Statistics *statistics;

typedef std::map<std::string, IOComponent*> DeviceList;
extern DeviceList devices;
IOComponent* lookup_device(const std::string name);

std::map<std::string, std::string>message_handlers;


extern bool program_done;

bool IODCommandGetStatus::run(std::vector<std::string> &params) {
       if (params.size() == 2) {
    MachineInstance *machine = MachineInstance::find(params[1].c_str());
    if (machine) {
	done = true;
	result_str = machine->getCurrentStateString();
    }
           else
               error_str = "Not Found";
       }
       return done;
   }

bool IODCommandSetStatus::run(std::vector<std::string> &params) {
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
            MachineInstance *mi = MachineInstance::find(ds.c_str());
            if (mi) {
                /* it would be safer to push the requested state change onto the machine's
                    action list but some machines do not poll their action list because they
                    do not expect to receive events
                */
                if (mi->transitions.size()) {
				   SetStateActionTemplate ssat(CStringHolder(strdup(ds.c_str())), State(params[3].c_str()) );
				   mi->active_actions.push_front(ssat.factory(mi)); // execute this state change once all other actions are complete
                }
                else {
                    mi->setState(params[3].c_str());
                }
                return true;
            }
        }
        //  Send reply back to client
        const char *msg_text = "Not found: ";
        size_t len = strlen(msg_text) + ds.length();
        char *text = (char *)malloc(len+1);
        sprintf(text, "%s%s", msg_text, ds.c_str());
        error_str = text;
        return false;
    }
    error_str = "Usage: SET device TO state";
    return false;
}


bool IODCommandEnable::run(std::vector<std::string> &params) {
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

bool IODCommandResume::run(std::vector<std::string> &params) {
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

    bool IODCommandDisable::run(std::vector<std::string> &params) {
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


    bool IODCommandDescribe::run(std::vector<std::string> &params) {
        std::cout << "received iod command Describe " << params[1] << "\n";
        bool use_json = false;
        if (params.size() == 3 && params[2] != "JSON") {
            error_str = "Usage: DESCRIBE machine [JSON]";
            return false;
        }
        if (params.size() == 2 || params.size() == 3) {
            MachineInstance *m = MachineInstance::find(params[1].c_str());
            cJSON *root;
            if (use_json)
                root = cJSON_CreateArray();
            std::stringstream ss;
            if (m)
                    m->describe(ss);
            else
                ss << "Failed to describe unknown machine " << params[1];
            if (use_json) {
                std::istringstream iss(ss.str());
                char buf[500];
                while (iss.getline(buf, 500, '\n')) {
                    cJSON_AddItemToArray(root, cJSON_CreateString(buf));                
                }
                char *res = cJSON_Print(root);
                cJSON_Delete(root);
                result_str = res;
                delete res;
            }
            else
                result_str = ss.str();
            return true;
        }
        error_str = "Failed to find machine";
        return false;
    }


    bool IODCommandToggle::run(std::vector<std::string> &params) {
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
				     Message *msg;
                    if (m->getCurrent().getName() == "on") {
                        if (m->receives(Message("turnOff"), 0)) {
                            msg = new Message("turnOff");
                            m->send(msg, m);
                        }
                        else
                            m->setState("off");
                    }
                    else if (m->getCurrent().getName() == "off") {
                        if (m->receives(Message("turnOn"), 0)) {
                            msg = new Message("turnOn");
                            m->send(msg, m);
                        }
                        else
                            m->setState("on");
                    }
                    result_str = "OK";
                    return true;
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

    bool IODCommandProperty::run(std::vector<std::string> &params) {
        if (params.size() == 4) {
		    MachineInstance *m = MachineInstance::find(params[1].c_str());
		    if (m) {
				if (m->debug()) {
					DBG_MSG << "setting property " << params[1] << "." << params[2] << " to " << params[3] << "\n";
				}
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
			std::stringstream ss;
			ss << "Unrecognised parameters in ";
			std::ostream_iterator<std::string> out(ss, " ");
			std::copy(params.begin(), params.end(), out);
			ss << ".  Usage: PROPERTY property_name value";
			error_str = ss.str();
			return false;
		}
	}

    bool IODCommandList::run(std::vector<std::string> &params) {
        std::ostringstream ss;
        std::map<std::string, MachineInstance*>::const_iterator iter = machines.begin();
        while (iter != machines.end()) {
            MachineInstance *m = (*iter).second;
            ss << (m->getName()) << " " << m->_type;
            if (m->_type == "POINT") ss << " " << m->properties.lookup("tab");
            ss << "\n";
            iter++;
        }
        result_str = ss.str();
        return true;
    }

std::set<std::string> IODCommandListJSON::no_display;

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
			if (IODCommandListJSON::no_display.count(prop.first)) continue;
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


    bool IODCommandListJSON::run(std::vector<std::string> &params) {
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
                for (unsigned int idx = 0; idx < m->locals.size(); ++idx) {
                    const Parameter &p = m->locals[idx];
    	       		cJSON_AddItemToArray(root, printMachineInstanceToJSON(p.machine, m->getName()));
	        	}
			}
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


/*
	send a message. The message may be in one of the forms: 
		machine-object.command or
		object.command
*/
    bool IODCommandSend::run(std::vector<std::string> &params) {
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
			result_str = ss.str();
			return true;
		}
		else {
			ss << "Could not find machine "<<machine_name<<" for command " << command << std::flush;
		}
        error_str = ss.str();
        return false;
    }

    bool IODCommandQuit::run(std::vector<std::string> &params) {
        program_done = true;
        std::stringstream ss;
        ss << "quitting ";
        std::ostream_iterator<std::string> oi(ss, " ");
        ss << std::flush;
        result_str = ss.str();
        return true;
    }

    bool IODCommandHelp::run(std::vector<std::string> &params) {
        std::stringstream ss;
        ss 
		 << "Commands: \n"
		 << "DEBUG machine on|off\n"
		 << "DEBUG debug_group on|off\n"
         << "DESCRIBE machine_name [JSON]\n"
		 << "DISABLE machine_name\n"
		 << "EC command\n"
		 << "ENABLE machine_name\n"
		 << "GET machine_name\n"
		 << "LIST JSON\n"
		 << "LIST\n"
		 << "MASTER\n"
		 << "MODBUS EXPORT\n"
		 << "MODBUS group address new_value\n"
         << "MODBUS REFRESH\n"
		 << "PROPERTY machine_name property new_value\n"
		 << "QUIT\n"
		 << "RESUME machine_name\n"
		 << "SEND command\n"
		 << "SET machine_name TO state_name\n"
		 << "SLAVES\n"
		 << "TOGGLE output_name\n"
		;
		std::string s = ss.str();
        result_str = s;
        return true;
    }

    bool IODCommandDebugShow::run(std::vector<std::string> &params) {
		std::stringstream ss;
		ss << "Debug status: \n" << *LogState::instance() << "\n" << std::flush;
		std::string s = ss.str();
		result_str = s;
		return true;
	}

    bool IODCommandDebug::run(std::vector<std::string> &params) {
		if (params.size() != 3) {
			std::stringstream ss;
			ss << "usage: DEBUG debug_group on|off\n\nDebug groups: \n" << *LogState::instance() << std::flush;
			std::string s = ss.str();
			error_str = s;
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
				error_str = "Unknown debug group";
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
			error_str = "Please use 'on' or 'off' in all upper- or lowercase";
			return false;
		}
		result_str = "OK";
		return true;
	}

    bool IODCommandModbus::run(std::vector<std::string> &params) {
		if (params.size() != 4) {
			error_str = "Usage: MODBUS group address value";
			return false;
		}
		int group, address, val;
		char *end;
		group = (int)strtol(params[1].c_str(), &end, 0);
		if (*end) {
			error_str = "Modbus: group number should be from 1..4";
			return false;
		}
		address = (int)strtol(params[2].c_str(), &end, 0);
		if (*end) {
			error_str = "Modbus: address should be from 0..65535";
			return false;
		}
		val = (int)strtol(params[3].c_str(), &end, 0);
		if (*end) {
			std::stringstream ss;
			ss  << "Modbus: value (" << params[3] << ") was expected to be a number from 0..65535\n";
			std::string s = ss.str();
			error_str = s;
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
				error_str = "Modbus: ignoring unregistered address\n";
			}
		}
		else {
			std::stringstream ss;
			ss << "failed to find Modbus Address matching group: " << group << ", address: " << address;
			error_str = ss.str();
			return false;
		}
		result_str = "OK";
		return true;
	}

    bool IODCommandModbusExport::run(std::vector<std::string> &params) {

		const char *file_name = modbus_map();
		const char *backup_file_name = "modbus_mappings.bak";
		if (rename(file_name, backup_file_name)) {
			std::cerr << strerror(errno) << "\n";
		}
		std::list<MachineInstance*>::iterator m_iter = MachineInstance::begin();
		std::ofstream out(file_name);
		if (!out) {
			error_str = "not able to open mapping file for write";
			return false;
		}
		while (m_iter != MachineInstance::end()) {
			(*m_iter)->exportModbusMapping(out);
			m_iter++;
		}
		out.close();
		result_str = "OK";
		return true;
	}

    bool IODCommandModbusRefresh::run(std::vector<std::string> &params) {
		std::list<MachineInstance*>::iterator m_iter = MachineInstance::begin();
		std::stringstream out;
		while (m_iter != MachineInstance::end()) {
			(*m_iter)->refreshModbus(out);
			m_iter++;
		}
		std::string s(out.str());
		result_str = s;
		return true;
	}


    bool IODCommandUnknown::run(std::vector<std::string> &params) {
        std::stringstream ss;
        std::ostream_iterator<std::string> oi(ss, "");
        std::copy(params.begin(), params.end(), oi);
        
        if (message_handlers.count(ss.str())) {
            const std::string &name = message_handlers[ss.str()];
            MachineInstance *m = MachineInstance::find(name.c_str());
            if (m) {
                m->send(new Message(ss.str().c_str()),m);
                result_str = "OK";
                return true;
            }
        }
        ss << ": Unknown command";
        error_str = ss.str();
        return false;
    }

#ifdef USE_ETHERCAT

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
	cJSON *root = cJSON_CreateArray();
    MasterDevice m(0);
    m.open(MasterDevice::Read);
    
    ec_ioctl_master_t master = 0;
    ec_ioctl_slave_t slave = 0;
    m.getMaster(&master);
    
	for (unsigned int i=0; i<master.slave_count; i++) {
		m.getSlave(&slave, i);
        cJSON_AddItemToArray(root, generateSlaveCStruct(m, slave, reconfigure));
    }
	char *res = cJSON_Print(root);
	cJSON_Delete(root);
	return res;
}


    bool IODCommandEtherCATTool::run(std::vector<std::string> &params) {
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

    bool IODCommandGetSlaveConfig::run(std::vector<std::string> &params) {
        char *res = collectSlaveConfig(false);
        if (res) {
            result_str = res;
			free(res);
            return true;
        }
        else {
            error_str = "JSON Error";
            return false;
        }
    }

bool IODCommandMasterInfo::run(std::vector<std::string> &params) {
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
        free(res);
        done = true;
    }
    else {
        error_str = "JSON error";
        done = false;
    }
    cJSON_Delete(root);
    return done;
}

#else

bool IODCommandEtherCATTool::run(std::vector<std::string> &params) {
    error_str = "EtherCAT Tool is not available";
    return false;
}

bool IODCommandGetSlaveConfig::run(std::vector<std::string> &params) {
    cJSON *root = cJSON_CreateObject();
    char *res = cJSON_Print(root);
    cJSON_Delete(root);
    if (res) {
        result_str = res;
        free(res);
        return true;
    }
    else {
        error_str = "JSON Error";
        return false;
    }
}
bool IODCommandMasterInfo::run(std::vector<std::string> &params) {
    //const ec_master_t *master = ECInterface::instance()->getMaster();
    //const ec_master_state_t *master_state = ECInterface::instance()->getMasterState();
    extern Statistics *statistics;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "slave_count", 0);
    cJSON_AddNumberToObject(root, "link_up", 0);
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

#endif

void sendMessage(zmq::socket_t &socket, const char *message) {
    const char *msg = (message) ? message : "";
    size_t len = strlen(msg);
    zmq::message_t reply (len);
    memcpy ((void *) reply.data (), msg, len);
    socket.send (reply);
}

