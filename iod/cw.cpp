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


Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;


static boost::mutex q_mutex;
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


struct IODCommandGetSlaveConfig : public IODCommand {
	bool run(std::vector<std::string> &params);
};

struct IODCommandMasterInfo : public IODCommand {
	bool run(std::vector<std::string> &params);
};

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


struct IODCommandThread {
    void operator()() {
		std::cout << "------------------ Command Thread Started -----------------\n";
        zmq::context_t context (1);
        zmq::socket_t socket (context, ZMQ_REP);
        socket.bind ("tcp://*:5555");
        IODCommand *command = 0;
        
        while (!done) {
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
                        boost::mutex::scoped_lock lock(q_mutex);
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
                    else if (ds == "SLAVES") {
                        command = new IODCommandGetSlaveConfig;
                    }
                    else if (count == 1 && ds == "MASTER") {
                        command = new IODCommandMasterInfo;
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
                    //std::cout << command->result() << "\n";
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
    
    // redirect output to the logfile if given TBD use Logger::setOutputStream..
    if (logfilename && strcmp(logfilename, "-") != 0)
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
        
        int max_disc = 0;
		int max_coil = 0;
		int max_input = 0;
		int max_holding = 0;
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
					if (group_num == 0) { if (addr_num >= max_coil) max_coil = addr_num+1; }
					else if (group_num == 1) { if (addr_num >= max_disc) max_disc = addr_num+1; }
					else if (group_num == 3) { if (addr_num >= max_input) max_input = addr_num+1; }
					else if (group_num == 4) { if (addr_num >= max_holding) max_holding = addr_num+1; }
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
		if (max_disc>= ModbusAddress::nextDiscrete()) ModbusAddress::setNextDiscrete(max_disc+1);
		if (max_coil>= ModbusAddress::nextCoil()) ModbusAddress::setNextCoil(max_coil+1);
		if (max_input>= ModbusAddress::nextInputRegister()) ModbusAddress::setNextInputRegister(max_input+1);
		if (max_holding>= ModbusAddress::nextHoldingRegister()) ModbusAddress::setNextHoldingRegister(max_holding+1);
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
    //if (!opened_file) yyparse();
    
    if (!opened_file) return 1;
    
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
	flag->transitions.push_back(Transition(State("on"),State("off"),Message("turnOff")));
	flag->transitions.push_back(Transition(State("off"),State("on"),Message("turnOn")));
	
	MachineClass *mc_variable = new MachineClass("VARIABLE");
	mc_variable->states.push_back("ready");
    mc_variable->initial_state = State("ready");
	mc_variable->disableAutomaticStateChanges();
	mc_variable->parameters.push_back(Parameter("VAL_PARAM1"));
    mc_variable->options["VALUE"] = "VAL_PARAM1";
	mc_variable->properties.add("PERSISTENT", Value("true", Value::t_string), SymbolTable::ST_REPLACE);
	
	MachineClass *mc_constant = new MachineClass("CONSTANT");
	mc_constant->states.push_back("ready");
    mc_constant->initial_state = State("ready");
	mc_constant->disableAutomaticStateChanges();
	mc_constant->parameters.push_back(Parameter("VAL_PARAM1"));
    mc_constant->options["VALUE"] = "VAL_PARAM1";

	mc_constant->properties.add("PERSISTENT", Value("true", Value::t_string), SymbolTable::ST_REPLACE);

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
                    ss << "Warning: no instance " << p_i.sValue << " found for " << mi->getName();
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
			if (!source) {
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

	statistics = new Statistics;
	int load_result = loadConfig(argc, argv);
	if (load_result)
		return load_result;
    
    if (dependency_graph()) {
        std::ofstream graph(dependency_graph());
        if (graph) {
            graph << "digraph G {\n";
            std::list<MachineInstance *>::iterator m_iter;
            m_iter = MachineInstance::begin();
            while (m_iter != MachineInstance::end()) {
                MachineInstance *mi = *m_iter++;
                BOOST_FOREACH(MachineInstance *dep, mi->depends) {
                    graph << mi->getName() << " -> " << dep->getName()<< ";\n";
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
		boost::mutex::scoped_lock lock(q_mutex);

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
			//pause();
			struct timeval start_t, end_t;
			{
				gettimeofday(&start_t, 0);
				if (machine_is_ready) {
					delta = get_diff_in_microsecs(&start_t, &end_t);
					cycle_delay_stat->add(delta);
				}
				//if (ECInterface::sig_alarms != user_alarms)
                {
					ECInterface::instance()->collectState();
                    boost::mutex::scoped_lock lock(q_mutex);
			
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
        gettimeofday(&end_t, 0);
        delta = get_diff_in_microsecs(&end_t, &start_t);
        if (delta < 1000000/ECInterface::FREQUENCY)
            usleep(1000000/ECInterface::FREQUENCY - delta);

	}
    stateMonitor.stop();
    monitor.join();
	return 0;
}
