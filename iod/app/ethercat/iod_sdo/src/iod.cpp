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
#include <map>
#include <utility>
#include <fstream>
#include "cJSON.h"
#ifndef EC_SIMULATOR
#include <tool/MasterDevice.h>
#ifdef USE_SDO
#include "SDOEntry.h"
#endif //USE_SDO
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
// #include "DebugExtra.h"
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
#include "Channel.h"
#include "ethercat_xml_parser.h"

bool program_done = false;
bool machine_is_ready = false;
bool machine_was_ready = false;

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

std::list<DeviceInfo*>collected_configurations;
std::map<unsigned int, DeviceInfo*> slave_configuration;

class ClockworkDeviceConfigurator : public DeviceConfigurator {
	public:

		bool configure( DeviceInfo *dev) {
			std::cout << "collected configuration for device "
				<< std::hex << " 0x" << dev->product_code << " "
				<< std::hex << " 0x" << dev->revision_no
				<< "\n";
			std::list<DeviceInfo*>::iterator iter = collected_configurations.begin();
			while (iter != collected_configurations.end()) {
				const DeviceInfo *di = *iter++;
				if ( *dev == *di ) {
					std::cout << " using item already found\n";
					return true;
				}
			}
			collected_configurations.push_back(dev);
			return true;
		}
};

#ifndef EC_SIMULATOR
SlaveInfo getSlaveInfo(ec_slave_info_t &info) {
	SlaveInfo res;
	res.set_slave_info(info);
	return res;
}

void collectSlaves(std::vector<SlaveInfo> &slave_arr) {
	std::list<ec_slave_info_t> slaves;
	ECInterface::instance()->listSlaves(slaves);
	
	std::transform(slaves.begin(), slaves.end(), std::back_inserter(slave_arr), getSlaveInfo);
	std::cout << "found " << slave_arr.size() << " slaves on the bus\n";
}
#endif



class ModuleSpec {
	public:
		ModuleSpec(MachineInstance *m) :
		  position(safe_mutable_value(m, "position")),
		  xml_filename(safe_mutable_value(m, "config_file")),
		  selected_sm(safe_mutable_value(m, "alternate_sync_manager")),
		  product_code(safe_mutable_value(m, "ProductCode")),
		  revision_no(safe_mutable_value(m, "RevisionNo")) {
		}
        Value &safe_mutable_value(MachineInstance *m, const char *name) { Value *v = m->getMutableValue(name); return (v) ? *v : local_value; }
	Value local_value;
	Value &position;
	Value &xml_filename;
	Value &selected_sm ;
	Value &product_code;
	Value &revision_no;
};

DeviceInfo *find_configured_device(DeviceInfo &dev) {
	std::cout << "checking for " << dev<< "\n";
	DeviceInfo *di = 0;
	std::list<DeviceInfo*>::iterator iter = collected_configurations.begin();
	while (iter != collected_configurations.end()) {
		DeviceInfo *item = *iter++;
		if ( dev == *item ) {
			std::cout << " using item " << *item << "\n";
			di = item;
			break;
		}
		std::cout << "no match with " << *item << "\n";
	}
	return di;
}

void update_device_spec_from_bus(ModuleSpec &spec, unsigned int position, std::vector<SlaveInfo> &slave_arr) {
	if (position < slave_arr.size()) {
		if (spec.product_code == SymbolTable::Null)
			spec.product_code = (long)slave_arr[ position ].product_code;
		if (spec.revision_no == SymbolTable::Null)
			spec.revision_no = (long)slave_arr[ position ].revision;
		if (spec.selected_sm == SymbolTable::Null) 
		  spec.selected_sm = Value("", Value::t_string);
	}
}

void loadSlaveConfigurations() {
	//determine which EtherCAT modules are to use configurations loaded from
	// XML files
	// build a list of modules with xml configs and a list of xml file references
	//   collecting product code and revision numbers if specified
	// where product codes and revision numbers are not specified in the config
	//   the module in the bus position will be used
	// search the bus to complete the product_code/release_no details if necessary
	// search the xml files for matching modules
	ClockworkDeviceConfigurator configurator;
	EtherCATXMLParser parser(configurator);

	std::vector<SlaveInfo> slave_arr;
#ifndef EC_SIMULATOR
	collectSlaves(slave_arr);
#endif

	std::list<MachineInstance*>::iterator iter = MachineInstance::begin();
	std::set<std::string> xml_files;
	while (iter != MachineInstance::end())
	{
		const int error_buf_size = 100;
		char error_buf[error_buf_size];
		MachineInstance *m = *iter++;
		if (m->_type == "MODULE") {
			ModuleSpec seek(m);
			if (seek.position == SymbolTable::Null) continue; // MODULE does not have a position
			if (seek.xml_filename != SymbolTable::Null) {
				std::cout << "using xml configuration file " << seek.xml_filename 
					<< " for " << m->getName() << " at position " << seek.position << "\n";
				xml_files.insert(seek.xml_filename.sValue);
				update_device_spec_from_bus(seek, seek.position.iValue, slave_arr);
				
				parser.xml_configured.clear();
				DeviceInfo *dev = new DeviceInfo(seek.product_code.iValue, seek.revision_no.iValue, seek.selected_sm.sValue.c_str() );
				parser.xml_configured.push_back( dev );
				parser.init();
				if (!parser.loadDeviceConfigurationXML(seek.xml_filename.sValue.c_str())) {
					std::cerr << "Warning: failed to load module configuration from " 
       						<< seek.xml_filename<< "\n";
				}
				else {
					DeviceInfo *di = find_configured_device(*dev);
					if (di) {
						slave_configuration[seek.position.iValue] = di;
						const ec_slave_info_t &slave( *(slave_arr[ seek.position.iValue ].ec_info) );
						ECModule *module = new ECModule();

						module->name = slave.name;
						module->alias = slave.alias;
						module->position = slave.position;
						module->vendor_id = slave.vendor_id;
						module->product_code = slave.product_code;
						module->revision_no = slave.revision_number;
						module->syncs = di->config.c_syncs;
						module->pdos = 0;
						module->pdo_entries = di->config.c_entries;
						module->sync_count = di->config.num_syncs;
						module->entry_details = di->config.c_entry_details;
						module->num_entries = di->config.num_entries;
#ifndef EC_SIMULATOR
						if (!ECInterface::instance()->addModule(module, true))  {
							delete module; // module may be already registered
							std::cerr << "failed to add module " << module->name << "\n";
						}
#endif
					}
					else {
						std::cout << "error: found " << collected_configurations.size() 
							<< " for slave at position " << seek.position 
							<< " when searching xml file for device " 
							<< std::hex << seek.product_code.iValue << "/" << seek.revision_no.iValue << std::dec << ":" << seek.selected_sm.sValue << "\n";
					}
				}
			}
		}
	}
}

bool setupEtherCatThread() {
	if (!ECInterface::instance()->initialised) {
		std::cout << "Cannect setup the EtherCAT thread until the interface is initialised\n";
		exit(1);
	}
	loadSlaveConfigurations();
	{
#if 0
		// having collected a number of xml files we load the manual configurations
		std::set<std::string>::iterator fi(xml_files.begin());
		while (fi != xml_files.end()) {
			const std::string &fname = *fi++;
			parser.init();
			std::cout << "attempting to load devices from " << fname << "\n";
			if (!parser.loadDeviceConfigurationXML(fname.c_str())) {
				std::cerr << "Warning: failed to load module configuration from " << fname << "\n";
			}
		}
		std::cout << "Collected " << collected_configurations.size() << " configurations\n\n";
#endif
#ifndef EC_SIMULATOR
		char *slave_config = collectSlaveConfig(true);
		if (slave_config) free(slave_config);
		ECInterface::instance()->configureModules();
		ECInterface::instance()->registerModules();
#endif
	}
	generateIOComponentModules(slave_configuration);
#ifndef EC_SIMULATOR
#ifdef USE_SDO
	// prepare all SDO entries
	SDOEntry::resolveSDOModules(); 
#endif //USE_SDO
#endif
	IOComponent::setupIOMap();
	initialiseOutputs();
	return true;
}


class IODHardwareActivation : public HardwareActivation {
	public:
    IODHardwareActivation() : setup_done(false) {}
		bool initialiseHardware() {
      assert(!setup_done);
			if (setupEtherCatThread()) {
#ifdef USE_SDO
				ECInterface::instance()->beginModulePreparation();
#endif
				setup_done = true;
			}
			return setup_done;
		}
		void operator()(void) {
			NB_MSG << "----------- Initialising machines ------------\n";
			initialise_machines();
		}
private:
  bool setup_done;
};

int main(int argc, char const *argv[])
{
	char *pn = strdup(argv[0]);
	program_name = strdup(basename(pn));
	free(pn);
std::string thread_name("iod_main");
#ifdef __APPLE__
	pthread_setname_np(thread_name.c_str());
#else
	pthread_setname_np(pthread_self(), thread_name.c_str());
#endif

	std::cout << "main starting\n";
	zmq::context_t *context = new zmq::context_t;
	MessagingInterface::setContext(context);
	Logger::instance();
	Dispatcher::instance();
	MessageLog::setMaxMemory(10000);
	Scheduler::instance();

	std::cout << "-------- Creating Command Interface ---------\n";
	ControlSystemMachine machine;
	IODCommandThread *stateMonitor = IODCommandThread::instance();
	IODHardwareActivation iod_activation;
	ProcessingThread &processMonitor(ProcessingThread::create(&machine, iod_activation, *stateMonitor));


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
		ControlSystemMachine machine;
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

	const Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
	long delay = 1000;
	if (cycle_delay_v) delay = cycle_delay_v->iValue;
	ECInterface::FREQUENCY=1000000 / delay;

	MachineInstance *ethercat_status = MachineInstance::find("ETHERCAT");
	if (!ethercat_status) 
		std::cerr << "Warning: No instance of the EtherCAT control machine found\n";

	if (num_errors > 0) {
		// display errors and warnings
		BOOST_FOREACH(std::string &error, error_messages) {
			std::cerr << error << "\n";
			MessageLog::instance()->add(error.c_str());
		}
		// abort if there were errors
		std::cerr << "Errors detected. Aborting\n";
		return 2;
	}

	std::cout << "-------- Initialising ---------\n";

	std::cout << "-------- Starting EtherCAT Interface ---------\n";
	EtherCATThread ethercat;
	boost::thread ecat_thread(boost::ref(ethercat));

	std::cout << "-------- Starting Scheduler ---------\n";
	boost::thread scheduler_thread(boost::ref(*Scheduler::instance()));
	Scheduler::instance()->setThreadRef(scheduler_thread);

	boost::thread monitor(boost::ref(*stateMonitor));
	usleep(50000); // give time before starting the processin g thread

	// Inform the modbus interface we have started
	load_debug_config();
	ModbusAddress::message("STARTUP");
	Dispatcher::start();

	processMonitor.setProcessingThreadInstance(&processMonitor);
	boost::thread process(boost::ref(processMonitor));

	// let channels start processing messages
	Channel::startChannels();

	// do not start a thread, simply run this process directly
	//processMonitor();
	try {
		process.join();
		return 0;
		Dispatcher::instance()->stop();
		Scheduler::instance()->stop();
		stateMonitor-> stop();
		ethercat.stop();
		delete context;
	}
	catch (const zmq::error_t &error) { // expected error when we remove the zmq context
	}
	//monitor.join();
	return 0;
}
