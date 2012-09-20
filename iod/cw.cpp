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

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <list>
#include <map>
#include <utility>
#include <fstream>
#include <sys/stat.h>
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include <signal.h>

#include "cJSON.h"
#ifndef EC_SIMULATOR
#include "tool/MasterDevice.h"
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
#include "IODCommands.h"
#include "Statistics.h"
#include "clockwork.h"
#include "ClientInterface.h"

bool program_done = false;
bool machine_is_ready = false;

void usage(int argc, char *argv[]);
void displaySymbolTable();


Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;


static boost::mutex thread_protection_mutex;
static boost::condition_variable_any cond;


typedef std::map<std::string, IOComponent*> DeviceList;
DeviceList devices;

//IOComponent* lookup_device(const std::string name);
//void checkInputs();

IOComponent* lookup_device(const std::string name) {
    DeviceList::iterator device_iter = devices.find(name);
    if (device_iter != devices.end()) 
        return (*device_iter).second;
    return 0;
}
// in a simulated environment, we provide a way to wire components together

typedef std::list<std::string> StringList;
std::map<std::string, StringList> wiring;



#if 0
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

static void finish(int sig)
{
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGINT, &sa, 0);
    program_done = true;
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


int main (int argc, char const *argv[])
{
	unsigned int user_alarms = 0;
	Logger::instance();
	ControlSystemMachine machine;

    Logger::instance()->setLevel(Logger::Debug);
	LogState::instance()->insert(DebugExtra::instance()->DEBUG_PARSER);
    //std::ofstream log("/tmp/cw.log");
    //if (log) Logger::instance()->setOutputStream(&log);

	load_debug_config();

	IODCommandListJSON::no_display.insert("tab");
	IODCommandListJSON::no_display.insert("type");
	IODCommandListJSON::no_display.insert("name");
	IODCommandListJSON::no_display.insert("image");
	IODCommandListJSON::no_display.insert("class");
	IODCommandListJSON::no_display.insert("state");
	IODCommandListJSON::no_display.insert("export");
	IODCommandListJSON::no_display.insert("startup_enabled");
	IODCommandListJSON::no_display.insert("NAME");
	IODCommandListJSON::no_display.insert("STATE");
	IODCommandListJSON::no_display.insert("PERSISTENT");

	statistics = new Statistics;
    int load_result = 0;
    {
        boost::mutex::scoped_lock lock(thread_protection_mutex);
        load_result = loadConfig(argc, argv);
        if (load_result)
            return load_result;
    }
    
    if (dependency_graph()) {
        std::ofstream graph(dependency_graph());
        if (graph) {
            graph << "digraph G {\n";
            std::list<MachineInstance *>::iterator m_iter;
            m_iter = MachineInstance::begin();
            while (m_iter != MachineInstance::end()) {
                MachineInstance *mi = *m_iter++;
                if (!mi->depends.empty()) {
                    BOOST_FOREACH(MachineInstance *dep, mi->depends) {
                        graph << mi->getName() << " -> " << dep->getName()<< ";\n";
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

	//ECInterface::instance()->start();

	std::list<Output *> output_list;
	{
		boost::mutex::scoped_lock lock(thread_protection_mutex);

		size_t remaining = machines.size();
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
					int bit_position = (int)m->parameters[1].val.iValue;
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
					int module_position = (int)module_mi->properties.lookup("position").iValue;
					if (module_position == -1)  { // module position unmapped
						std::cerr << "Machine " << name << " position not mapped\n";
						continue; 
					}
				
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

	std::cout << "-------- Initialising ---------\n";	
	
	std::list<MachineInstance *>::iterator m_iter;

	if (persistent_store()) {
		// load the store into a map
		typedef std::pair<std::string, Value> PropertyPair;
		std::map<std::string, std::list<PropertyPair> >init_values;
		std::ifstream store(persistent_store());
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
			if (m && (m->_type == "CONSTANT" || m->getValue("PERSISTENT") == "true") ) {
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
    else {
		m_iter = MachineInstance::begin();
        while (m_iter != MachineInstance::end()) {
			MachineInstance *m = *m_iter++;
			if (m && (m->_type == "CONSTANT" || m->getValue("PERSISTENT") == "true") ) {
				m->enable();
            }
        }
    }
    
    // prepare the list of machines that will be processed at idle time
    m_iter = MachineInstance::begin();
    int num_passive = 0;
    int num_active = 0;
    while (m_iter != MachineInstance::end()) {
        MachineInstance *mi = *m_iter++;
        if (!mi->receives_functions.empty() || mi->commands.size()
            || (mi->getStateMachine() && !mi->getStateMachine()->transitions.empty())
            || mi->isModbusExported()
            || mi->uses_timer ) {
            mi->markActive();
            DBG_INITIALISATION << mi->getName() << " is active\n";
            ++num_active;
        }
        else {
            mi->markPassive();
            DBG_INITIALISATION << mi->getName() << " is passive\n";
            ++num_passive;
        }
    }
    std::cout << num_passive << " passive and " << num_active << " active machines\n";
    

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

    setup_signals();
    
	std::cout << "-------- Starting Command Interface ---------\n";	
	IODCommandThread stateMonitor;
	boost::thread monitor(boost::ref(stateMonitor));
	
	Statistic *cycle_delay_stat = new Statistic("Cycle Delay");
	Statistic::add(cycle_delay_stat);
	long delta, delta2;

	// Inform the modbus interface we have started
	load_debug_config();
	ModbusAddress::message("STARTUP");

	while (!program_done)
    {
        struct timeval start_t, end_t;
        {
            boost::mutex::scoped_lock lock(thread_protection_mutex); // obtain exclusive access during main loop processing
			{
				gettimeofday(&start_t, 0);
				if (machine_is_ready) {
					delta = get_diff_in_microsecs(&start_t, &end_t);
					cycle_delay_stat->add(delta);
				}
				//if (ECInterface::sig_alarms != user_alarms)
                {
					ECInterface::instance()->collectState();
			
					gettimeofday(&end_t, 0);
					delta = get_diff_in_microsecs(&end_t, &start_t);
					statistics->io_scan_time.add(delta);
					delta2 = delta;
			
			        //checkInputs(); // simulated wiring between inputs and outputs
                    machine.idle();
					if (machine.connected()) {

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
                        {
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

        gettimeofday(&end_t, 0);
        delta = get_diff_in_microsecs(&end_t, &start_t);
        if (delta < 1000000/ECInterface::FREQUENCY)
            usleep(1000000/ECInterface::FREQUENCY - delta);

	}
    stateMonitor.stop();
    monitor.join();
	return 0;
}
