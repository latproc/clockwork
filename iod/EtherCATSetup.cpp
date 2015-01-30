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


extern boost::mutex thread_protection_mutex;

std::list<MachineInstance *>output_points;

void initialiseOutputs() {
	std::list<MachineInstance *>default_outputs;
	std::list<MachineInstance *>::iterator iter = output_points.begin();
	while (iter != output_points.end()) {
		MachineInstance *m = *iter++;
		Value &val = m->properties.lookup("default");
		if (val == SymbolTable::Null) continue;
		default_outputs.push_back(m);
		if (val.kind == Value::t_integer || m->_type == "ANALOGOUTPUT") {
			std::cout << "Initialising value for " << m->getName() << " to " << val << "\n";
			long i_val = 0;
			if (val.asInteger(i_val)) {
				m->setValue("VALUE", i_val);
				if (m->io_interface) m->io_interface->setValue(i_val);
				else { std::cout << " warning: " << m->getName() << " is not connected to hardware\n"; }
			}
		}
		else  {
			std::cout << "Initialising state of " << m->getName() << " to " << val << "\n";
			m->setState(val.asString().c_str());
		}
	}
	IOComponent::setDefaultData(IOComponent::getProcessData());
	IOComponent::setDefaultMask(IOComponent::generateMask(default_outputs));
} 

void generateIOComponentModules()
{
	std::list<Output *> output_list;
	{
		boost::mutex::scoped_lock lock(thread_protection_mutex);

		int remaining = machines.size();
		std::cout << remaining << " Machines\n";
		std::cout << "Linking clockwork machines to hardware\n";
		std::map<std::string, MachineInstance*>::const_iterator iter = machines.begin();
		while (iter != machines.end())
		{
			const int error_buf_size = 100;
			char error_buf[error_buf_size];
			MachineInstance *m = (*iter).second;
			iter++;
			--remaining;
			if ( (m->_type == "POINT" || m->_type == "ANALOGINPUT"  || m->_type == "COUNTERRATE"
						|| m->_type == "COUNTER" 
						|| m->_type == "STATUS_FLAG" || m->_type == "ANALOGOUTPUT" ) && m->parameters.size() > 1)
			{
				output_points.push_back(m);
				// points should have two parameters, the name of the io module and the bit offset
				//Parameter module = m->parameters[0];
				//Parameter offset = m->parameters[1];
				//Value params = p.val;
				//if (params.kind == Value::t_list && params.listValue.size() > 1) {
				std::string name;
				unsigned int entry_position = 0;
				if (m->_type == "COUNTERRATE")
				{
					name = m->parameters[1].real_name;
					entry_position = m->parameters[2].val.iValue;
				}
				else
				{
					name = m->parameters[0].real_name;
					entry_position = m->parameters[1].val.iValue;
				}

				std::cerr << "Setting up point " << m->getName() 
					<< " " << entry_position << " on module " << name << "\n";
				MachineInstance *module_mi = MachineInstance::find(name.c_str());
				if (!module_mi)
				{
					snprintf(error_buf, error_buf_size, "No machine called %s", name.c_str());
					MessageLog::instance()->add(error_buf);
					std::cerr << error_buf << "\n";
					error_messages.push_back(error_buf);
					++num_errors;
					continue;
				}
				if (!module_mi->properties.exists("position"))   // module position not given
				{
					snprintf(error_buf, error_buf_size, "Machine %s does not specify a position", 
						module_mi->getName().c_str());
					MessageLog::instance()->add(error_buf);
					std::cerr << error_buf << "\n";
					error_messages.push_back(error_buf);
					++num_errors;
					continue;
				}
				if (m->_type == "ANALOGOUTPUT") {
					Value &default_value = m->properties.lookup("default");
					if (default_value == SymbolTable::Null) {
						snprintf(error_buf, error_buf_size, 
							"Machine %s must specify a default value", m->getName().c_str());
						MessageLog::instance()->add(error_buf);
						std::cerr << error_buf << "\n";
						error_messages.push_back(error_buf);
						++num_errors;
					}
				}
				std::cerr << module_mi->properties.lookup("position").kind << "\n";
				int module_position = module_mi->properties.lookup("position").iValue;
				if (module_position == -1)    // module position unmapped
				{
					snprintf(error_buf, error_buf_size, "Machine %s position not mapped", name.c_str());
					MessageLog::instance()->add(error_buf);
					std::cerr << error_buf << "\n";
					continue;
				}

#ifndef EC_SIMULATOR
				// some modules have multiple io types (eg the EK1814) and therefore
				// multiple syncmasters, we number
				// the points from 1..n but the device numbers them 1.n,1.m,..., resetting
				// the index for each sync master.
				ECModule *module = ECInterface::findModule(module_position);
				if (!module)
				{
					snprintf(error_buf, error_buf_size, "No module found at position %d for %s", 
						module_position, m->getName().c_str());
					MessageLog::instance()->add(error_buf);
					std::cerr << error_buf << "\n";
					error_messages.push_back(error_buf);
					++num_errors;
					continue;
				}

				if (entry_position >= module->num_entries)
				{
					snprintf(error_buf, error_buf_size, "No entry %d on module %d (%s)", 
							entry_position, module_position, name.c_str());
					MessageLog::instance()->add(error_buf);
					std::cerr << error_buf << "\n";
					continue; // could not find this device
				}
				EntryDetails *ed = &module->entry_details[entry_position];
				unsigned int direction = module->syncs[ed->sm_index].dir;
				unsigned int offset_idx = entry_position;
				unsigned int bitlen = module->pdo_entries[entry_position].bit_length;

				if (direction == EC_DIR_OUTPUT)
				{
					std::cerr << "Adding new output device " << m->getName()
						<< " position: " << entry_position
						<< " name: " << module->entry_details[offset_idx].name
						<< " bit_pos: " << module->bit_positions[offset_idx]
						<< " offset: " << module->offsets[offset_idx]
						<< " bitlen: " << bitlen <<  "\n";
					IOAddress addr (
							IOComponent::add_io_entry(ed->name.c_str(),
								module_position,
								module->offsets[offset_idx],
								module->bit_positions[offset_idx], offset_idx, bitlen));

					if (bitlen == 1)
					{
						Output *o = new Output(addr);
						output_list.push_back(o);
						IOComponent::devices[m->getName().c_str()] = o;
						o->setName(m->getName().c_str());
						m->io_interface = o;
						o->addDependent(m);
					}
					else
					{
						AnalogueOutput *o = new AnalogueOutput(addr);
						output_list.push_back(o);
						IOComponent::devices[m->getName().c_str()] = o;
						o->setName(m->getName().c_str());
						m->io_interface = o;
						o->addDependent(m);
					}
				}
				else
				{
					//sstr << m->getName() << "_IN_" << entry_position << std::flush;
					//const char *name_str = sstr.str().c_str();
					std::cerr << "Adding new input device " << m->getName()
						<< " position: " << entry_position
						<< " name: " << module->entry_details[offset_idx].name
						<< " sm_idx: " << ed->sm_index
						<< " bit_pos: " << module->bit_positions[offset_idx]
						<< " offset: " << module->offsets[offset_idx]
						<<  " bitlen: " << bitlen << "\n";
					IOAddress addr( IOComponent::add_io_entry(ed->name.c_str(),
								module_position,
								module->offsets[offset_idx],
								module->bit_positions[offset_idx], offset_idx, bitlen));

					if (bitlen == 1)
					{
						Input *in = new Input(addr);
						IOComponent::devices[m->getName().c_str()] = in;
						in->setName(m->getName().c_str());
						m->io_interface = in;
						in->addDependent(m);
					}
					else
					{
						if (m->_type == "COUNTERRATE")
						{
							CounterRate *in = new CounterRate(addr);
							char *nm = strdup(m->getName().c_str());
							IOComponent::devices[nm] = in;
							free(nm);
							in->setName(m->getName().c_str());
							m->io_interface = in;
							in->addDependent(m);
						}
						else if (m->_type == "COUNTER")
						{
							Counter *in = new Counter(addr);
							char *nm = strdup(m->getName().c_str());
							IOComponent::devices[nm] = in;
							free(nm);
							in->setName(m->getName().c_str());
							m->io_interface = in;
							in->addDependent(m);
						}
						else
						{
							AnalogueInput *in = new AnalogueInput(addr);
							char *nm = strdup(m->getName().c_str());
							IOComponent::devices[nm] = in;
							free(nm);
							in->setName(m->getName().c_str());
							m->io_interface = in;
							in->addDependent(m);
						}
					}
				}
#endif
			}
			else
			{
				if (m->_type != "POINT" && m->_type != "STATUS_FLAG" && m->_type != "COUNTERRATE"
						&& m->_type != "COUNTER"
						&& m->_type != "ANALOGINPUT" && m->_type != "ANALOGOUTPUT" )
					DBG_MSG << "Skipping " << m->_type << " " << m->getName() << " (not a POINT)\n";
				else
					DBG_MSG << "Skipping " << m->_type << " " << m->getName() << " (no parameters)\n";
			}
		}
		assert(remaining==0);
	}
}

