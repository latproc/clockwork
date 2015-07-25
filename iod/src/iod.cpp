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
#include <tool/MasterDevice.h>
#endif

#define __MAIN__
//#include "latprocc.h"
#include "MachineInstance.h"
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
#include "Statistics.h"
#include "IODCommands.h"
#include <signal.h>
#include "clockwork.h"
#include "ClientInterface.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "ecat_thread.h"
#include "ProcessingThread.h"
#include "EtherCATSetup.h"

bool program_done = false;
bool machine_is_ready = false;

void usage(int argc, char *argv[]);
void displaySymbolTable();

Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;

boost::mutex thread_protection_mutex;
static boost::mutex io_mutex;
static boost::mutex model_mutex;
boost::condition_variable_any io_updated;
boost::condition_variable_any model_updated;
boost::mutex ecat_mutex;
boost::condition_variable_any ecat_polltime;

#ifdef EC_SIMULATOR

// in a simulated environment, we provide a way to wire components together
void checkInputs();

typedef std::list<std::string> StringList;
std::map<std::string, StringList> wiring;


void checkInputs()
{
	std::map<std::string, StringList>::iterator iter = wiring.begin();
	while (iter != wiring.end())
	{
		const std::pair<std::string, StringList> &link = *iter++;
		Output *device = dynamic_cast<Output*>(IOComponent::lookup_device(link.first));
		if (device)
		{
			StringList::const_iterator linked_devices = link.second.begin();
			while (linked_devices != link.second.end())
			{
				Input *end_point = dynamic_cast<Input*>(IOComponent::lookup_device(*linked_devices++));
				SimulatedRawInput *in = 0;
				if (end_point)
					in  = dynamic_cast<SimulatedRawInput*>(end_point->access_raw_device());
				if (in)
				{
					if (device->isOn() && !end_point->isOn())
					{
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
#endif

void load_debug_config()
{
	if (debug_config()) {
		std::ifstream program_config(debug_config());
		if (program_config)
		{
			std::string debug_flag;
			while (program_config >> debug_flag)
			{
				if (debug_flag[0] == '#') continue;
				int dbg = LogState::instance()->lookup(debug_flag);
				if (dbg) LogState::instance()->insert(dbg);
				else if (machines.count(debug_flag))
				{
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
	std::cerr << "received signal..quitting\n";
	program_done = true;
	io_updated.notify_one();
}

bool setup_signals()
{
	struct sigaction sa;
	sa.sa_handler = finish;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGTERM, &sa, 0) || sigaction(SIGINT, &sa, 0))
	{
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
			//ECInterface::instance()->activate();
			initialise_machines();
		}
};

int main(int argc, char const *argv[])
{
	std::cout << "main starting\n";
	zmq::context_t *context = new zmq::context_t;
	MessagingInterface::setContext(context);
	Logger::instance();
	Dispatcher::instance();
	MessageLog::setMaxMemory(10000);
	Scheduler::instance();
	ControlSystemMachine machine;

	Logger::instance()->setLevel(Logger::Debug);
	//LogState::instance()->insert(DebugExtra::instance()->DEBUG_PARSER);

	std::list<std::string> source_files;
	int load_result = loadOptions(argc, argv, source_files);
	if (load_result) return load_result;
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
	if (load_result)
		return load_result;
	if (dependency_graph())
	{
		std::cout << "writing dependency graph to " << dependency_graph() << "\n";
		std::ofstream graph(dependency_graph());
		if (graph)
		{
			graph << "digraph G {\n";
			std::list<MachineInstance *>::iterator m_iter;
			m_iter = MachineInstance::begin();
			while (m_iter != MachineInstance::end())
			{
				MachineInstance *mi = *m_iter++;
				if (!mi->depends.empty())
				{
					BOOST_FOREACH(MachineInstance *dep, mi->depends)
					{
						graph << mi->getName() << " -> " << dep->getName() << ";\n";
					}
				}
			}
			graph << "}\n";
		}
		else
		{
			std::cerr << "not able to open " << dependency_graph() << " for write\n";
		}
	}

	if (test_only() )
	{
		const char *backup_file_name = "modbus_mappings.bak";
		rename(modbus_map(), backup_file_name);
		// export the modbus mappings and exit
		std::list<MachineInstance*>::iterator m_iter = MachineInstance::begin();
		std::ofstream out(modbus_map());
		if (!out)
		{
			std::cerr << "not able to open " << modbus_map() << " for write\n";
			return false;
		}
		while (m_iter != MachineInstance::end())
		{
			(*m_iter)->exportModbusMapping(out);
			m_iter++;
		}
		out.close();

		return load_result;
	}

	Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
	long delay = 1000;
	if (cycle_delay_v) cycle_delay_v->iValue = delay;
	ECInterface::FREQUENCY=1000000 / delay;

#ifndef EC_SIMULATOR
	{
		char *slave_config = collectSlaveConfig(true);
		if (slave_config) free(slave_config);
	}
#endif
	generateIOComponentModules();
	IOComponent::setupIOMap();
	initialiseOutputs();

if (num_errors > 0) {
	// display errors and warnings
	BOOST_FOREACH(std::string &error, error_messages) {
		std::cerr << error << "\n";
	}
	// abort if there were errors
	std::cerr << "Errors detected. Aborting\n";
	return 2;
}


#ifdef EC_SIMULATOR
	wiring["EL2008_OUT_3"].push_back("EL1008_IN_1");
#endif

	std::cout << "-------- Initialising ---------\n";
	ECInterface::instance()->activate();

	setup_signals();

	std::cout << "-------- Starting EtherCAT Interface ---------\n";
	EtherCATThread ethercat;
	boost::thread ecat_thread(boost::ref(ethercat));

	std::cout << "-------- Starting Scheduler ---------\n";
	boost::thread scheduler_thread(boost::ref(*Scheduler::instance()));


	std::cout << "-------- Starting Command Interface ---------\n";
	IODCommandThread &stateMonitor(*IODCommandThread::instance());
	boost::thread monitor(boost::ref(stateMonitor));

	// Inform the modbus interface we have started
	load_debug_config();
	ModbusAddress::message("STARTUP");
	Dispatcher::start();

	IODHardwareActivation iod_activation;
	ProcessingThread processMonitor(machine, iod_activation, stateMonitor);
	boost::thread process(boost::ref(processMonitor));
	// do not start a thread, simply run this process directly
	//processMonitor();
	try {
		process.join();
		return 0;
		Dispatcher::instance()->stop();
		Scheduler::instance()->stop();
		stateMonitor.stop();
		ethercat.stop();
		delete context;
	}
	catch (zmq::error_t) { // expected error when we remove the zmq context
	}
	monitor.join();
	return 0;
}
