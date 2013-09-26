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

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdlib.h>
#include <inttypes.h>
#include <list>
#include <string>

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include <sys/stat.h>

#include "options.h"
#include "ModbusInterface.h"
#include "Logger.h"
#include "DebugExtra.h"
#include "MachineInstance.h"
#include "clockwork.h"

extern int yylineno;
extern int yycharno;
const char *yyfilename = 0;
extern FILE *yyin;
int yyparse();
int yylex(void);
SymbolTable globals;


std::list<std::string>error_messages;
int num_errors = 0;


void usage(int argc, char const *argv[])
{
    std::cerr << "Usage: " << argv[0] << " [-v] [-l logfilename] [-i persistent_store] [-c debug_config_file] [-m modbus_mapping] [-g graph_output] [-s maxlogfilesize] \n";
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

void load_preset_modbus_mappings() {
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
					group_num = (int)strtol(group.c_str(), &end, 10);
					if (*end) {
						std::cerr << "error loading modbus mappings from " << modbus_map()
						<< " at line " << lineno << " " << *end << "not expected\n";
						++errors;
						continue;
					}
					addr_num = (int)strtol(addr.c_str(), &end, 10);
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
}

void semantic_analysis() {
    MachineClass *point_class = new MachineClass("POINT");
    point_class->parameters.push_back(Parameter("module"));
    point_class->parameters.push_back(Parameter("offset"));
    point_class->states.push_back("on");
    point_class->states.push_back("off");
	point_class->default_state = State("off");
	point_class->initial_state = State("off");
	point_class->disableAutomaticStateChanges();

    point_class = new MachineClass("STATUS_FLAG");
    point_class->parameters.push_back(Parameter("module"));
    point_class->parameters.push_back(Parameter("offset"));
    point_class->parameters.push_back(Parameter("entry"));
    point_class->states.push_back("on");
    point_class->states.push_back("off");
	point_class->default_state = State("off");
	point_class->initial_state = State("off");
	point_class->disableAutomaticStateChanges();
    
    
    MachineClass *ain_class = new MachineClass("ANALOGINPUT");
    ain_class->parameters.push_back(Parameter("module"));
    ain_class->parameters.push_back(Parameter("offset"));
    ain_class->parameters.push_back(Parameter("entry"));
    ain_class->states.push_back("stable");
    ain_class->states.push_back("unstable");
	ain_class->default_state = State("stable");
	ain_class->initial_state = State("stable");
	ain_class->disableAutomaticStateChanges();
	ain_class->properties.add("VALUE", Value(0), SymbolTable::ST_REPLACE);
    
    MachineClass *module_class = new MachineClass("MODULE");
	module_class->disableAutomaticStateChanges();

    MachineClass *publisher_class = new MachineClass("PUBLISHER");
    publisher_class->parameters.push_back(Parameter("broker"));
    publisher_class->parameters.push_back(Parameter("topic"));
    publisher_class->parameters.push_back(Parameter("message"));
    
    MachineClass *subscriber_class = new MachineClass("SUBSCRIBER");
    subscriber_class->parameters.push_back(Parameter("broker"));
    subscriber_class->parameters.push_back(Parameter("topic"));
    subscriber_class->options["message"] = "";

    MachineClass *broker_class = new MachineClass("MQTTBROKER");
    broker_class->parameters.push_back(Parameter("host"));
    broker_class->parameters.push_back(Parameter("port"));
	broker_class->disableAutomaticStateChanges();
    
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
    
    MachineClass *mc_external = new MachineClass("EXTERNAL");
    mc_external->options["HOST"] = "localhost";
    mc_external->options["PORT"] = 5600;
    
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
            
			size_t n = mc->stable_states.size();
			size_t i;
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
    
        // check the parameter count of the instantiated machine vs the machine class and raise an error if necessary
        m_iter = MachineInstance::begin();
        while (m_iter != MachineInstance::end()) {
            MachineInstance *mi = *m_iter++;
        std::cout << "Machine " << mi->getName() << " has " << mi->parameters.size() << " parameters\n";
        
		if (mi->getStateMachine() && mi->parameters.size() != mi->getStateMachine()->parameters.size()) {
            // the POINT class special; it can have either 2 or 3 parameters (yuk)
            if (mi->getStateMachine()->name != "POINT" || mi->getStateMachine()->parameters.size() < 2 || mi->getStateMachine()->parameters.size() >3) {
                std::stringstream ss;
                ss << "## - Error: Machine " << mi->getStateMachine()->name << " requires "
                << mi->getStateMachine()->parameters.size()
                << " parameters but instance " << mi->getName() << " has " << mi->parameters.size() << std::flush;
                std::string s = ss.str();
                ++num_errors;
                error_messages.push_back(s);
            }
		}
        
        // for each of the machine instance's symbol parameters,
        //  find a reference to the machine instance corresponding to the given name
        //  and raise a warning if necessary. ( should this warning be an error?)
        for (unsigned int i=0; i<mi->parameters.size(); i++) {
            Value p_i = mi->parameters[i].val;
            if (p_i.kind == Value::t_symbol) {
                std::cout << "  parameter " << i << " " << p_i.sValue << " (" << mi->parameters[i].real_name << ")\n";
				MachineInstance *found = mi->lookup(mi->parameters[i]); // uses the real_name field and caches the result
				if (found) {
                    // special check of parameter types for points
                    if (mi->_type == "POINT" && i == 0 && found->_type != "MODULE" &&  found->_type != "MQTTBROKER") {
                        std::cout << "Error: in the definition of " << mi->getName() << ", " <<
                        p_i.sValue << " has type " << found->_type << " but should be MODULE or MQTTBROKER\n";
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
            
            MachineInstance *m = mi->locals[i].machine;
            // fixup real names of parameters that are passed as parameters to our locals
			for (unsigned int j=0; j<m->parameters.size(); ++j) {
				Parameter &p = m->parameters[j];
                if (p.val.kind == Value::t_symbol) {
                    for (unsigned int k = 0; k < mi->parameters.size(); ++k) {
                        Value p_k = mi->parameters[k].val;
                        if (p_k.kind == Value::t_symbol && p.val == p_k) {
                            p.real_name = mi->parameters[k].real_name;
                            break;
                        }
                    }
                    // no matching name in the parameter list, is a local machine being passed?
                    for (unsigned int k=0; k<mi->locals.size(); ++k) {
                        if (p.val == mi->locals[k].machine->getName()) {
                            p.real_name = mi->getName() + ".";
                            p.real_name += p.val.sValue;
                            m->parameters[j].machine = mi->locals[k].machine;
                            break;
                        }
                    }
                }
            }
			for (unsigned int j=0; j<m->parameters.size(); ++j) {
				Parameter &p = m->parameters[j];
				if (p.val.kind == Value::t_symbol) {
					DBG_MSG << "      " << j << ": " << p.val << "\n";
					p.machine = mi->lookup(p);
					if (p.machine) {
						p.machine->addDependancy(m);
						m->listenTo(p.machine);
						DBG_MSG << " linked parameter " << j << " of local " << m->getName()
                            << " (" << m->parameters[j].val << ") to " << p.machine->getName() << "\n";
					}
					else {
						std::stringstream ss;
						ss << "## - Error: local " << m->getName() << " for machine " << mi->getName()
                            << " sends an unknown parameter " << p.val << " at position: " << j << "\n";
						error_messages.push_back(ss.str());
						++num_errors;
					}
				}
			}
		}
	}
    
	// setup triggered actions for the stable states for each machine
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
				//DBG_MSG << "duplicating receive function for " << mi->getName() << " from " << source->getName() << " (" << machine << ")" << "\n";
				event = source->getName() + "." + event;
				mi->receives_functions[Message(event.c_str())] = rcv.second;
			}
		}
	}
	
	// reorder the list of machines in reverse order of dependence
	MachineInstance::sort();
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
		else if (*(argv[i]) == '-') {
			files.push_back(argv[i]);
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
	
    std::cout << (argc-1) << " arguments\n";
    
    load_preset_modbus_mappings();
    
    
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
    
    semantic_analysis();
	
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


void initialise_machines() {
	
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
            || mi->uses_timer
            || mi->mq_interface
            ) {
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
    
}

