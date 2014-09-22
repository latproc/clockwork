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
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, 0)) return false;
    return true;
}

int main (int argc, char const *argv[])
{
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
	Logger::instance();
    MessageLog::setMaxMemory(10000);

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
	IODCommandListJSON::no_display.insert("POLLING_DELAY");
	IODCommandListJSON::no_display.insert("TRACEABLE");

	statistics = new Statistics;
    int load_result = 0;
    {
        load_result = loadConfig(argc, argv);
        if (load_result) {
            Dispatcher::instance()->stop();
            return load_result;
        }
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
	
    MQTTInterface::instance()->init();
    MQTTInterface::instance()->start();
    
	std::list<Output *> output_list;
	{
        {
            size_t remaining = machines.size();
            std::cout << remaining << " Machines\n";
            std::cout << "Linking POINTs to hardware\n";
            
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
                    if (m->_type != "POINT" && m->_type != "PUBLISHER" && m->_type != "SUBSCRIBER")
                        DBG_MSG << "Skipping " << m->_type << " " << m->getName() << " (not a POINT)\n";
                    else   {
                        std::string name = m->parameters[0].real_name;
                        DBG_MSG << "Setting up " << m->_type << " " << m->getName() << " \n";
                        MQTTModule *module = MQTTInterface::instance()->findModule(m->parameters[0].real_name);
                        if (!module) {
                            std::cerr << "No MQTT Broker called " << m->parameters[0].real_name << "\n";
                            continue;
                        }
                        std::string topic = m->parameters[1].val.asString();
                        if (m->_type != "SUBSCRIBER" && m->parameters.size() == 3) {
                            if (!module->publishes(topic)) {
                                m->properties.add("type", "Output");
                                module->publish(topic, m->parameters[2].val.asString(), m);
                            }
                        }
                        else if (m->_type != "PUBLISHER") {
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

	std::cout << "-------- Initialising ---------\n";
	initialise_machines();
    
    bool sigok = setup_signals();
    assert(sigok);
    
    //zmq::socket_t resource_mgr(*MessagingInterface::getContext(), ZMQ_REP);
    //resource_mgr.bind("inproc://resource_mgr");
    
    zmq::socket_t sim_io(*MessagingInterface::getContext(), ZMQ_REP);
    sim_io.bind("inproc://ethercat_sync");
    
    std::cout << "-------- Starting Scheduler ---------\n";
    boost::thread scheduler_thread(boost::ref(*Scheduler::instance()));
    
	std::cout << "-------- Starting Command Interface ---------\n";	
	IODCommandThread stateMonitor;
	boost::thread monitor(boost::ref(stateMonitor));

	// Inform the modbus interface we have started
	load_debug_config();
	ModbusAddress::message("STARTUP");

    Dispatcher::start();

    //zmq::socket_t process_sync(*MessagingInterface::getContext(), ZMQ_REP);
    //process_sync.bind("inproc://process_sync");

    ControlSystemMachine machine;
    ProcessingThread processMonitor(machine);
	boost::thread process(boost::ref(processMonitor));
    
    MQTTInterface::instance()->activate();
/*
    int processing_sequence = processMonitor.sequence;
    {
        size_t ps_len = 0;
        char buf[10];
        while (ps_len == 0) {
            try {
                ps_len = process_sync.recv(buf,10);
            } catch (zmq::error_t) {
                std::cerr << zmq_strerror(errno) << "\n";
            }
        }
    }
 */

    char buf[10];
    size_t response_len;
    safeRecv(sim_io, buf, 10, true, response_len);
    
    std::cerr << "processing has started\n";
    struct timeval then;
    gettimeofday(&then,0);
    while (!program_done) {
        MQTTInterface::instance()->collectState();
/*        {
            process_sync.send("go", 2);
            size_t ps_len = 0;
            char buf[10];
            while (ps_len == 0) {
                try {
                    ps_len = process_sync.recv(buf,10);
                } catch (zmq::error_t) {
                    std::cerr << zmq_strerror(errno) << "\n";
                }
            }
        }
        if (processing_sequence != processMonitor.sequence) { // did the model update?
            ++processing_sequence;
        }
        {   // handshake to give the command handler access to shared resources for a while
            // if it has requested it.
            char buf[10];
            while (resource_mgr.recv(buf, 10, ZMQ_NOBLOCK)) {
                resource_mgr.send("go", 2);
                size_t msglen = 0;
                while ( (msglen = resource_mgr.recv(buf, 10) ) == 0 ) ;
                resource_mgr.send("ok",2);
            }
        }
        
        zmq::pollitem_t *poll_items = 0;
        int active_channels = Channel::pollChannels(poll_items, 20, 0);
        if (active_channels) {
            Channel::handleChannels();
        }
 */
        sim_io.send("ecat", 4);
        safeRecv(sim_io, buf, 10, true, response_len);
        struct timeval now;
        gettimeofday(&now,0);
        
        int64_t delta = (uint64_t)(now.tv_sec - then.tv_sec) * 1000000 + ( (uint64_t)now.tv_usec - (uint64_t)then.tv_usec);
        // use the clockwork interpreter's current cycle delay
        Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
        assert(cycle_delay_v);
        int64_t delay = cycle_delay_v->iValue;
        if (delay <= 1000) delay = 1000;
        if (delay-delta > 50){
            delay = delay-delta-50                                                           ;
            //std::cout << "waiting: " << delay << "\n";
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
    stateMonitor.stop();
    monitor.join();
        delete context;
    }
    catch (zmq::error_t) {
        
    }
	return 0;
}
