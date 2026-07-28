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

#include "ControlSystemMachine.h"
#include "EtherCATEntrySelector.h"
#include "ECInterface.h"
#include "IOComponent.h"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include <sstream>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zmq.hpp>

#include "cJSON.h"
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <fstream>
#include <list>
#include <map>
#include <utility>
#ifndef EC_SIMULATOR
#include <iostream>
#endif

#define __MAIN__
//#include "latprocc.h"
#include "ClientInterface.h"
#include "DebugExtra.h"
#include "Dispatcher.h"
#include "IODCommand.h"
#include "IODCommands.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "ModbusInterface.h"
#include "PredicateAction.h"
#include "ProcessingThread.h"
#include "Scheduler.h"
#include "Statistic.h"
#include "Statistics.h"
#include "clockwork.h"
#include "ecat_thread.h"
#include "options.h"
#include "symboltable.h"
#include <signal.h>
#include <stdio.h>
#ifndef EC_SIMULATOR
#include "ethercat_xml_parser.h"
#include <ecrt.h>
#endif

extern boost::mutex thread_protection_mutex;

std::list<MachineInstance *> output_points;
extern std::list<std::string> error_messages;
extern int num_errors;

#ifndef EC_SIMULATOR
static bool getPDOIndex(const ECModule *module, const EntryDetails &details,
                        uint16_t &pdo_index) {
    if (module->pdos) {
        pdo_index = module->pdos[details.pdo_index].index;
        return true;
    }
    if (!module->syncs) {
        return false;
    }
    if (details.sm_index >= module->sync_count) {
        return false;
    }
    const ec_sync_info_t &sync = module->syncs[details.sm_index];
    if (!sync.pdos || details.pdo_index >= sync.n_pdos) {
        return false;
    }
    pdo_index = sync.pdos[details.pdo_index].index;
    return true;
}
#endif

static bool pushAnalogOrIntegerDefault(MachineInstance *m, const Value &val, bool log) {
    if (!m) {
        return false;
    }
    if (!(val.kind == Value::t_integer || m->_type == "ANALOGOUTPUT")) {
        return false;
    }
    long i_val = 0;
    if (!val.asInteger(i_val)) {
        return false;
    }
    if (log) {
        std::cout << "Initialising value for " << m->getName() << " to " << val << "\n";
    }
    m->setValue("VALUE", i_val);
    if (m->io_interface) {
        if (m->io_interface->address.is_signed) {
            m->io_interface->setValue((int32_t)(i_val & 0xfffffffff));
        }
        else {
            m->io_interface->setValue((uint32_t)(i_val & 0xfffffffff));
        }
    }
    else if (log) {
        std::cout << " warning: " << m->getName() << " is not connected to hardware\n";
    }
    return true;
}

void reapplyOutputDefaults() {
    // Prefer VALUE already set on the machine (plant may have updated it);
    // fall back to the configured `default` property.
    // Friend of MachineInstance only for setState; analog path uses setValue.
    unsigned pushed = 0;
    std::list<MachineInstance *>::iterator iter = output_points.begin();
    while (iter != output_points.end()) {
        MachineInstance *m = *iter++;
        if (!m || !m->io_interface) {
            continue;
        }
        const Value &cur = m->getValue("VALUE");
        const Value &def = m->properties.lookup("default");
        const Value *use = nullptr;
        if (!(cur.isNull() || cur.kind == Value::t_empty)) {
            // ANALOGOUTPUT: if VALUE is still 0 but plant `default` is non-zero,
            // prefer default (first seed before plant writes a real command).
            long cur_i = 0;
            long def_i = 0;
            const bool cur_ok = cur.asInteger(cur_i);
            const bool def_ok = !(def == SymbolTable::Null) && def.asInteger(def_i);
            if (m->_type == "ANALOGOUTPUT" && cur_ok && cur_i == 0 && def_ok && def_i != 0) {
                use = &def;
            }
            else {
                use = &cur;
            }
        }
        else if (!(def == SymbolTable::Null)) {
            use = &def;
        }
        if (!use) {
            continue;
        }
        if (pushAnalogOrIntegerDefault(m, *use, false)) {
            ++pushed;
        }
        else if (!(def == SymbolTable::Null) && m->_type != "ANALOGOUTPUT") {
            m->setState(def.asString().c_str());
            ++pushed;
        }
    }
    if (pushed) {
        std::cerr << "reapplyOutputDefaults: pushed " << pushed
                  << " output value(s) into process image\n";
    }
}

void initialiseOutputs() {
    std::list<MachineInstance *> default_outputs;
    std::list<MachineInstance *>::iterator iter = output_points.begin();
    while (iter != output_points.end()) {
        MachineInstance *m = *iter++;
        const Value &val = m->properties.lookup("default");
        if (val == SymbolTable::Null) {
            continue;
        }
        default_outputs.push_back(m);
        if (!pushAnalogOrIntegerDefault(m, val, true)) {
            std::cout << "Initialising state of " << m->getName() << " to " << val << "\n";
            m->setState(val.asString().c_str());
        }
    }
    IOComponent::setDefaultData(IOComponent::getProcessData()); // takes a copy
    uint8_t *dm = IOComponent::generateMask(default_outputs);
    IOComponent::setDefaultMask(dm); // takes a copy
    delete[] dm;
}

void cleanupIOComponentModules() { IOComponent::reset(); }

void generateIOComponentModules(std::map<unsigned int, DeviceInfo *> slave_configuration) {
    cleanupIOComponentModules();
    {
        boost::mutex::scoped_lock lock(thread_protection_mutex);
        output_points.clear();

        std::cout << "Linking clockwork machines to hardware\n";
        std::list<MachineInstance *>::iterator iter = MachineInstance::begin();
        while (iter != MachineInstance::end()) {
            const int error_buf_size = 256;
            char error_buf[error_buf_size];
            MachineInstance *m = *iter++;
            if ((m->_type == "POINT" || m->_type == "ANALOGINPUT" || m->_type == "COUNTERRATE" ||
                 m->_type == "COUNTER" || m->_type == "STATUS_FLAG" || m->_type == "DIGITALVALUE"
                 || m->_type == "ANALOGOUTPUT") &&
                m->parameters.size() > 1) {
                output_points.push_back(m);
                // points should have two parameters, the name of the io module and the bit offset
                //Parameter module = m->parameters[0];
                //Parameter offset = m->parameters[1];
                //Value params = p.val;
                //if (params.kind == Value::t_list && params.listValue.size() > 1) {
                std::string name;
                unsigned int entry_position = 0;
                size_t selector_index_param = 1;
                if (m->_type == "COUNTERRATE") {
                    name = m->parameters[1].real_name;
                    entry_position = m->parameters[2].val.iValue;
                    selector_index_param = 2;
                }
                else {
                    name = m->parameters[0].real_name;
                    entry_position = m->parameters[1].val.iValue;
                }
                int64_t positional_index = 0;
                int64_t positional_subindex = 0;
                int64_t positional_pdo_index = 0;
                bool has_positional_selector =
                    m->parameters.size() > selector_index_param + 1 &&
                    m->parameters[selector_index_param].val.asInteger(positional_index) &&
                    m->parameters[selector_index_param + 1].val.asInteger(positional_subindex);
                bool has_positional_pdo = false;
                if (has_positional_selector &&
                    m->parameters.size() > selector_index_param + 2) {
                    has_positional_pdo =
                        m->parameters[selector_index_param + 2].val.asInteger(
                            positional_pdo_index);
                }

#if 0
                std::cerr << "Setting up point " << m->getName()
                        << " " << entry_position << " on module " << name << "\n";
#endif
                MachineInstance *module_mi = MachineInstance::find(name.c_str());
                if (!module_mi) {
                    snprintf(error_buf, error_buf_size, "No machine called %s", name.c_str());
                    MessageLog::instance()->add(error_buf);
                    std::cerr << error_buf << "\n";
                    error_messages.push_back(error_buf);
                    ++num_errors;
                    continue;
                }
                if (!module_mi->properties.exists("position")) { // module position not given
                    snprintf(error_buf, error_buf_size, "Machine %s does not specify a position",
                             module_mi->getName().c_str());
                    MessageLog::instance()->add(error_buf);
                    std::cerr << error_buf << "\n";
                    error_messages.push_back(error_buf);
                    ++num_errors;
                    continue;
                }
                if (m->_type == "ANALOGOUTPUT") {
                    const Value &default_value = m->properties.lookup("default");
                    if (default_value == SymbolTable::Null) {
                        snprintf(error_buf, error_buf_size,
                                 "Machine %s must specify a default value", m->getName().c_str());
                        MessageLog::instance()->add(error_buf);
                        std::cerr << error_buf << "\n";
                        error_messages.push_back(error_buf);
                        ++num_errors;
                    }
                }
                int module_position = module_mi->properties.lookup("position").iValue;
                if (module_position == -1) { // module position unmapped
                    snprintf(error_buf, error_buf_size, "Machine %s position not mapped",
                             name.c_str());
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
                if (!module) {
                    snprintf(error_buf, error_buf_size, "No module found at position %d for %s",
                             module_position, m->getName().c_str());
                    MessageLog::instance()->add(error_buf);
                    std::cerr << error_buf << "\n";
                    error_messages.push_back(error_buf);
                    ++num_errors;
                    continue;
                }

                const bool has_entry_index = m->properties.exists("entry_index");
                const bool has_entry_subindex = m->properties.exists("entry_subindex");
                const bool has_entry_pdo_index = m->properties.exists("entry_pdo_index");
                if (has_positional_selector ||
                    has_entry_index || has_entry_subindex || has_entry_pdo_index) {
                    if (has_positional_selector &&
                        (has_entry_index || has_entry_subindex || has_entry_pdo_index)) {
                        snprintf(error_buf, error_buf_size,
                                 "Machine %s mixes positional and property EtherCAT selectors",
                                 m->getName().c_str());
                        MessageLog::instance()->add(error_buf);
                        std::cerr << error_buf << "\n";
                        error_messages.push_back(error_buf);
                        ++num_errors;
                        continue;
                    }
                    if (!has_entry_index || !has_entry_subindex) {
                        if (has_positional_selector) {
                            // Both values were validated while detecting the positional form.
                        }
                        else {
                        snprintf(error_buf, error_buf_size,
                                 "Machine %s must specify both entry_index and entry_subindex",
                                 m->getName().c_str());
                        MessageLog::instance()->add(error_buf);
                        std::cerr << error_buf << "\n";
                        error_messages.push_back(error_buf);
                        ++num_errors;
                        continue;
                        }
                    }

                    int64_t object_index = positional_index;
                    int64_t object_subindex = positional_subindex;
                    int64_t object_pdo_index = positional_pdo_index;
                    const Value &index_value = m->properties.lookup("entry_index");
                    const Value &subindex_value = m->properties.lookup("entry_subindex");
                    const Value &pdo_index_value = m->properties.lookup("entry_pdo_index");
                    if ((!has_positional_selector &&
                         (!index_value.asInteger(object_index) ||
                          !subindex_value.asInteger(object_subindex) ||
                          (has_entry_pdo_index &&
                           !pdo_index_value.asInteger(object_pdo_index)))) ||
                        object_index < 0 ||
                        object_index > UINT16_MAX ||
                        object_subindex < 0 ||
                        object_subindex > UINT8_MAX ||
                        ((has_entry_pdo_index || has_positional_pdo) &&
                         (object_pdo_index < 0 || object_pdo_index > UINT16_MAX))) {
                        snprintf(error_buf, error_buf_size,
                                 "Machine %s has an invalid EtherCAT entry object selector",
                                 m->getName().c_str());
                        MessageLog::instance()->add(error_buf);
                        std::cerr << error_buf << "\n";
                        error_messages.push_back(error_buf);
                        ++num_errors;
                        continue;
                    }

                    std::vector<EtherCATEntryIdentity> identities;
                    identities.reserve(module->num_entries);
                    bool valid_pdo_metadata = true;
                    for (unsigned int i = 0; i < module->num_entries; ++i) {
                        const EntryDetails &details = module->entry_details[i];
                        uint16_t pdo_index = 0;
                        if (!getPDOIndex(module, details, pdo_index)) {
                            valid_pdo_metadata = false;
                            break;
                        }
                        identities.push_back({i, module->pdo_entries[i].index,
                                              module->pdo_entries[i].subindex, pdo_index});
                    }
                    if (!valid_pdo_metadata) {
                        snprintf(error_buf, error_buf_size,
                                 "Machine %s has inconsistent PDO metadata on module %s",
                                 m->getName().c_str(), name.c_str());
                        MessageLog::instance()->add(error_buf);
                        std::cerr << error_buf << "\n";
                        error_messages.push_back(error_buf);
                        ++num_errors;
                        continue;
                    }

                    const EtherCATEntryMatch match = resolveEtherCATEntry(
                        identities, static_cast<uint16_t>(object_index),
                        static_cast<uint8_t>(object_subindex),
                        has_entry_pdo_index || has_positional_pdo,
                        static_cast<uint16_t>(object_pdo_index), entry_position);
                    if (match != EtherCATEntryMatch::found) {
                        snprintf(error_buf, error_buf_size,
                                 "Machine %s EtherCAT entry 0x%04X:%02X%s%s on module %s",
                                 m->getName().c_str(), static_cast<unsigned int>(object_index),
                                 static_cast<unsigned int>(object_subindex),
                                 match == EtherCATEntryMatch::ambiguous
                                     ? " is ambiguous"
                                     : " was not found",
                                 (has_entry_pdo_index || has_positional_pdo)
                                     ? " with the requested PDO" : "",
                                 name.c_str());
                        MessageLog::instance()->add(error_buf);
                        std::cerr << error_buf << "\n";
                        error_messages.push_back(error_buf);
                        ++num_errors;
                        continue;
                    }
                }

                if (entry_position >= module->num_entries) {
                    snprintf(error_buf, error_buf_size, "No entry %d on module %d (%s)",
                             entry_position, module_position, name.c_str());
                    MessageLog::instance()->add(error_buf);
                    std::cerr << error_buf << "\n";
                    error_messages.push_back(error_buf);
                    ++num_errors;
                    continue;
                }
                EntryDetails *ed = &module->entry_details[entry_position];
                unsigned int direction = module->syncs[ed->sm_index].dir;
                unsigned int offset_idx = entry_position;
                unsigned int bitlen = module->pdo_entries[entry_position].bit_length;

                if (direction == EC_DIR_OUTPUT) {
                    DBG_ETHERCAT << "Adding new output device " << m->getName()
                                 << " position: " << entry_position
                                 << " name: " << module->entry_details[offset_idx].name
                                 << " bit_pos: " << module->bit_positions[offset_idx]
                                 << " offset: " << module->offsets[offset_idx]
                                 << " bitlen: " << bitlen << "\n";
                    IOAddress addr(IOComponent::add_io_entry(
                        ed->name.c_str(), module_position, module->offsets[offset_idx],
                        module->bit_positions[offset_idx], offset_idx, bitlen));

                    if (bitlen == 1) {
                        Output *o = new Output(addr);
                        IOComponent::devices[m->getName().c_str()] = o;
                        o->setName(m->getName().c_str());
                        m->io_interface = o;
                        o->addDependent(m);
                        o->addOwner(m);
                    }
                    else {
                        AnalogueOutput *o = new AnalogueOutput(addr);
                        IOComponent::devices[m->getName().c_str()] = o;
                        o->setName(m->getName().c_str());
                        m->io_interface = o;
                        o->addDependent(m);
                        o->addOwner(m);
                        o->setupProperties(m);
                    }
                }
                else {
                    //sstr << m->getName() << "_IN_" << entry_position << std::flush;
                    //const char *name_str = sstr.str().c_str();
#if 1
                    DBG_ETHERCAT << "Adding new input device " << m->getName()
                                 << " position: " << entry_position
                                 << " name: " << module->entry_details[offset_idx].name
                                 << " sm_idx: " << std::hex << ed->sm_index << std::dec
                                 << " bit_pos: " << module->bit_positions[offset_idx]
                                 << " offset: " << module->offsets[offset_idx]
                                 << " bitlen: " << bitlen << "\n";
#endif
                    IOAddress addr(IOComponent::add_io_entry(
                        ed->name.c_str(), module_position, module->offsets[offset_idx],
                        module->bit_positions[offset_idx], offset_idx, bitlen));

                    // bitlen 1 → digital Input (on/off). Multi-bit POINT/STATUS_FLAG
                    // must not fall through to AnalogueInput (leaves CW state
                    // "undefined" and publishes bogus ENG). Use DigitalValue.
                    if (bitlen == 1) {
                        Input *in = new Input(addr);
                        IOComponent::devices[m->getName().c_str()] = in;
                        in->setName(m->getName().c_str());
                        m->io_interface = in;
                        in->addDependent(m);
                        in->addOwner(m);
                    }
                    else if (m->_type == "POINT" || m->_type == "STATUS_FLAG" ||
                             m->_type == "DIGITALVALUE") {
                        auto dv = new DigitalValue(addr);
                        char *nm = strdup(m->getName().c_str());
                        IOComponent::devices[nm] = dv;
                        free(nm);
                        dv->setName(m->getName().c_str());
                        m->io_interface = dv;
                        dv->addDependent(m);
                        dv->addOwner(m);
                        if (m->_type == "POINT" || m->_type == "STATUS_FLAG") {
                            std::cerr << "Warning: " << m->getName() << " is " << m->_type
                                      << " but EtherCAT entry bitlen is " << bitlen
                                      << " — using DigitalValue (VALUE), not on/off Input\n";
                        }
                    }
                    else if (m->_type == "COUNTERRATE") {
                        CounterRate *in = new CounterRate(addr);
                        char *nm = strdup(m->getName().c_str());
                        IOComponent::devices[nm] = in;
                        free(nm);
                        in->setName(m->getName().c_str());
                        m->io_interface = in;
                        in->addDependent(m);
                        in->addOwner(m);
                        m->setNeedsThrottle(true);
                        in->setupProperties(m);
                    }
                    else if (m->_type == "COUNTER") {
                        Counter *in = new Counter(addr);
                        char *nm = strdup(m->getName().c_str());
                        IOComponent::devices[nm] = in;
                        free(nm);
                        in->setName(m->getName().c_str());
                        m->io_interface = in;
                        in->addDependent(m);
                        in->addOwner(m);
                        in->setupProperties(m);
                        m->setNeedsThrottle(true);
                    }
                    else {
                        AnalogueInput *in = new AnalogueInput(addr);
                        char *nm = strdup(m->getName().c_str());
                        IOComponent::devices[nm] = in;
                        free(nm);
                        in->setName(m->getName().c_str());
                        m->io_interface = in;
                        in->addDependent(m);
                        in->addOwner(m);
                        in->setupProperties(m);
                        m->setNeedsThrottle(true);
                    }
                }
#endif
            }
            else {
#if 0
                if (m->_type != "POINT" && m->_type != "STATUS_FLAG" && m->_type != "COUNTERRATE"
                        && m->_type != "COUNTER"
                        && m->_type != "ANALOGINPUT" && m->_type != "ANALOGOUTPUT") {
                    DBG_MSG << "Skipping " << m->_type << " " << m->getName() << " (not a POINT)\n";
                }
                else {
                    DBG_MSG << "Skipping " << m->_type << " " << m->getName() << " (no parameters)\n";
                }
#endif
            }
        }
    }
}
