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
//#include "ECInterface.h"
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
#include <sys/stat.h>

#include "cJSON.h"
#ifndef EC_SIMULATOR
#include "tool/MasterDevice.h"
#endif

#define __MAIN__
#include "cwlang.h"
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
#include "MQTTInterface.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "Channel.h"
#include "ProcessingThread.h"
#include <libgen.h>

bool program_done = false;
bool machine_is_ready = false;

void usage(int argc, char *argv[]);
void displaySymbolTable();


Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;


//static boost::mutex io_mutex;
boost::condition_variable_any ecat_polltime;


typedef std::list<std::string> StringList;
std::map<std::string, StringList> wiring;


void load_debug_config() {
	if (debug_config()) {
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
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, 0)) return false;
    return true;
}

class IODHardwareActivation : public HardwareActivation {
	public:
		void operator()(void) {
			NB_MSG << "----------- Initialising machines ------------\n";
			initialise_machines();
		}
};

int main (int argc, char const *argv[])
{
	char *pn = strdup(argv[0]);
	program_name = strdup(basename(pn));
	free(pn);

	std::string thread_name("cw_main");
#ifdef __APPLE__
	pthread_setname_np(thread_name.c_str());
#else
	pthread_setname_np(pthread_self(), thread_name.c_str());
#endif

	zmq::context_t *context = new zmq::context_t;
	MessagingInterface::setContext(context);
	Logger::instance();
	Dispatcher::instance();
	MessageLog::setMaxMemory(10000);
	Scheduler::instance();
	ControlSystemMachine machine;

	set_debug_config("iod.conf");
	Logger::instance()->setLevel(Logger::Debug);
	LogState::instance()->insert(DebugExtra::instance()->DEBUG_PARSER);

	std::list<std::string> source_files;
	int load_result = loadOptions(argc, argv, source_files);
	if (load_result) return load_result;

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
	IODCommandListJSON::no_display.insert("POLLING_DELAY");
	IODCommandListJSON::no_display.insert("TRACEABLE");
	IODCommandListJSON::no_display.insert("default");

	statistics = new Statistics;
	load_result = loadConfig(source_files);
	if (load_result) {
		Dispatcher::instance()->stop();
		return load_result;
	}
	load_debug_config();

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
            return 1;
        }    
        while (m_iter != MachineInstance::end()) {
            (*m_iter)->exportModbusMapping(out);
            m_iter++;
        }    
        out.close();

		return load_result;
	}
	if (export_to_c()) {
		const char *export_path = "/tmp/cw_export";
		std::list<MachineClass*>::iterator iter = MachineClass::all_machine_classes.begin();
		if (mkdir(export_path, 0770) == -1 && errno != EEXIST) {
			std::cerr << "failed to create export directory /tmp/cw_export.. aborting\n";
			return 1;
		}
		while (iter != MachineClass::all_machine_classes.end()) {
			MachineClass *mc = *iter++;
			if (mc->name == "POINT") continue;
			if (mc->name == "SYSTEM") continue;
			char basename[80];
			snprintf(basename, 80, "%s/cw_%s", export_path, mc->name.c_str());
			const std::string fname(basename);
			mc->cExport(fname);
		}
		return 0;
	}
	
	MQTTInterface::instance()->init();
	MQTTInterface::instance()->start();

	std::list<Output *> output_list;
	{
        {
            size_t remaining = machines.size();
            DBG_PARSER << remaining << " Machines\n";
            DBG_INITIALISATION << "Initialising MQTT\n";
            
            // find and process all MQTT Modules as they are required before POINTS that use them
            std::map<std::string, MachineInstance*>::const_iterator iter = machines.begin();
            while (iter != machines.end()) {
                MachineInstance *m = (*iter).second; iter++;
                if (m->_type == "MQTTBROKER" && m->parameters.size() == 2) {
                    MQTTModule *module = MQTTInterface::instance()->findModule(m->getName());
                    if (module) {
                        std::cerr << "MQTT Broker " << m->getName() << " is already registered\n";
                        continue;
                    }
                    module = new MQTTModule(m->getName().c_str());
                    module->host = m->parameters[0].val.asString();
                    long port;
                    if (m->parameters[1].val.asInteger(port)) {
                        module->port = (int)port;
                        MQTTInterface::instance()->addModule(module, false);
                        module->connect();
                    }
                }
            }
            
            iter = machines.begin();
            while (iter != machines.end()) {
                MachineInstance *m = (*iter).second; iter++;
                --remaining;
                if (m->_type == "MQTTBROKER" && m->parameters.size() == 2) {
                    continue;
                }
                else if (m->_type == "POINT" && m->parameters.size() > 1 && m->parameters[1].val.kind == Value::t_integer) {
                        std::string name = m->parameters[0].real_name;
                        //int bit_position = (int)m->parameters[1].val.iValue;
                        //std::cerr << "Setting up point " << m->getName() << " " << bit_position << " on module " << name << "\n";
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
                    if (m->_type != "POINT" && m->_type != "MQTTPUBLISHER" && m->_type != "MQTTSUBSCRIBER") {
                        //DBG_MSG << "Skipping " << m->_type << " " << m->getName() << " (not a POINT)\n";
										}
                    else   {
                        std::string name = m->parameters[0].real_name;
                        //DBG_MSG << "Setting up " << m->_type << " " << m->getName() << " \n";
                        MQTTModule *module = MQTTInterface::instance()->findModule(m->parameters[0].real_name);
                        if (!module) {
                            std::cerr << "No MQTT Broker called " << m->parameters[0].real_name << "\n";
                            continue;
                        }
                        std::string topic = m->parameters[1].val.asString();
                        if (m->_type != "MQTTSUBSCRIBER" && m->parameters.size() == 3) {
                            if (!module->publishes(topic)) {
                                m->properties.add("type", "Output");
                                module->publish(topic, m->parameters[2].val.asString(), m);
                            }
                        }
                        else if (m->_type != "MQTTPUBLISHER") {
                            if (!module->subscribes(topic)) {
                                m->properties.add("type", "Input");
                                module->subscribe(topic, m);
                            }
                        }
                        else std::cerr << "Error defining instance " << m->getName() << "\n";
                        
                    }
                }
            }
            assert(remaining==0);
        }
	}
	//MachineInstance::displayAll();
#ifdef EC_SIMULATOR
	wiring["EL2008_OUT_3"].push_back("EL1008_IN_1");
#endif

	bool sigok = setup_signals();
	assert(sigok);

	IODCommandThread *stateMonitor = IODCommandThread::instance();
	IODHardwareActivation iod_activation;
	ProcessingThread &processMonitor(ProcessingThread::create(&machine, iod_activation, *stateMonitor));

	zmq::socket_t sim_io(*MessagingInterface::getContext(), ZMQ_REP);
	sim_io.bind("inproc://ethercat_sync");
    
	DBG_INITIALISATION << "-------- Starting Scheduler ---------\n";
	boost::thread scheduler_thread(boost::ref(*Scheduler::instance()));
	Scheduler::instance()->setThreadRef(scheduler_thread);

	DBG_INITIALISATION << "-------- Starting Command Interface ---------\n";
	boost::thread monitor(boost::ref(*stateMonitor));

	// Inform the modbus interface we have started
	load_debug_config();
	ModbusAddress::message("STARTUP");
	Dispatcher::start();

	processMonitor.setProcessingThreadInstance(&processMonitor);
	boost::thread process(boost::ref(processMonitor));
    
    MQTTInterface::instance()->activate();

    char buf[100];
    size_t response_len;
	if (safeRecv(sim_io, buf, 100, true, response_len, 0) ) {
		buf[ (response_len<100) ? response_len : 99 ] = 0;
		NB_MSG << "simulated io got start message: " << buf << "\n";
	}

    NB_MSG << "processing has started\n";
    uint64_t then = microsecs();
    while (!program_done) {
        MQTTInterface::instance()->collectState();

        //sim_io.send("ecat", 4);
        safeRecv(sim_io, buf, 10, false, response_len, 100);
        uint64_t now = microsecs();
        
        int64_t delta = now - then;
        // use the clockwork interpreter's current cycle delay
        const Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
        assert(cycle_delay_v);
        int64_t delay = cycle_delay_v->iValue;
        if (delay <= 1000) delay = 1000;
        if (delay-delta > 50){
            delay = delay-delta-50                                                           ;
            //NB_MSG << "waiting: " << delay << "\n";
            struct timespec sleep_time;
            sleep_time.tv_sec = delay / 1000000;
            sleep_time.tv_nsec = (delay * 1000) % 1000000000L;
            int rc;
            struct timespec remaining;
            while ( (rc = nanosleep(&sleep_time, &remaining) == -1) ) {
                sleep_time = remaining;
            }
        }
         
        then = now;
    }
    try {
    MQTTInterface::instance()->stop();
    Dispatcher::instance()->stop();
    processMonitor.stop();
    kill(0, SIGTERM); // interrupt select() and poll()s to enable termination
    process.join();
    stateMonitor->stop();
    monitor.join();
        delete context;
    }
    catch (zmq::error_t) {
        
    }
	return 0;
}
