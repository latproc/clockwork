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

#include "options.h"
#include <iostream>
#include <map>
#include <stdlib.h>
#include <string.h>
#include <string>

static int opt_verbose = 0;
static int opt_test_only = 0;
const char *persistent_store_name = 0;
const char *modbus_map_name = 0;
const char *debug_config_name = 0;
const char *dependency_graph_name = 0;
const char *dependency_graph_root = 0;
static int publisher_port_num = 5556;
static bool publisher_port_num_required = false;
static int persistent_port_num = 5557;
static int modbus_port_num = 5558;
static int command_port_num = 5555;
static bool command_port_num_required = false;
static bool keep_stats = false;
static char *dev_name = strdup("CLOCKWORK");
static bool is_tracing = false;
static unsigned long cycle_time_ = 1000;
static bool c_export = false;
static bool fix_invalid_auto_transitions = false;
static std::string ethercat_adapter_name;

static std::map<std::string, int> thread_cpu_affinities;

const char *device_name() { return dev_name; }
void set_device_name(const char *new_name) {
    if (dev_name) {
        free(dev_name);
    }
    dev_name = strdup(new_name);
}

void set_verbose(int trueOrFalse) { opt_verbose = trueOrFalse; }

int verbose() { return opt_verbose; }

void set_test_only(int trueOrFalse) { opt_test_only = trueOrFalse; }

int test_only() { return opt_test_only; }

void set_persistent_store(const char *name) { persistent_store_name = name; }

const char *persistent_store() { return persistent_store_name; }

void set_modbus_map(const char *name) { modbus_map_name = name; }

const char *modbus_map() { return modbus_map_name; }

void set_debug_config(const char *name) { debug_config_name = name; }

const char *debug_config() { return debug_config_name; }

void set_dependency_graph(const char *name) { dependency_graph_name = name; }

const char *dependency_graph() { return dependency_graph_name; }

const char *graph_root() { return dependency_graph_root; }

void set_graph_root(const char *root) { dependency_graph_root = root; }

void set_publisher_port(int port, bool required) {
    publisher_port_num = port;
    publisher_port_num_required = required;
}

int publisher_port() { return publisher_port_num; }

bool publisher_port_fixed() { return publisher_port_num_required; }

void set_persistent_store_port(int port) { persistent_port_num = port; }

int persistent_store_port() { return persistent_port_num; }

void set_modbus_port(int port) { modbus_port_num = port; }

int modbus_port() { return modbus_port_num; }

void set_command_port(int port, bool required) {
    command_port_num = port;
    command_port_num_required = required;
}

int command_port() { return command_port_num; }

bool command_port_fixed() { return command_port_num_required; }

void enable_statistics(bool which) { keep_stats = which; }

int keep_statistics() { return keep_stats; }

void enable_tracing(bool which) { is_tracing = which; }

bool tracing() { return is_tracing; }

void set_cycle_time(unsigned long new_time) { cycle_time_ = new_time; }
unsigned long get_cycle_time() { return cycle_time_; }

bool export_to_c() { return c_export; }
void set_export_to_c(bool which) { c_export = which; }

int cpu_affinity(const char *thread_name) {
    std::string property_name = thread_name;
    property_name += "_thread_cpu_affinity";
    std::map<std::string, int>::iterator found = thread_cpu_affinities.find(property_name);
    if (found != thread_cpu_affinities.end()) {
        return (*found).second;
    }
    return 0;
}

void set_cpu_affinity(const char *thread_name, int cpu) {
    std::string property_name = thread_name;
    property_name += "_thread_cpu_affinity";
    thread_cpu_affinities[property_name] = cpu;
}

bool fix_invalid_transitions() { return fix_invalid_auto_transitions; }
void set_fix_invalid_transitions(bool which) { fix_invalid_auto_transitions = which; }

void set_ethercat_adapter(const char *adapter) {
    ethercat_adapter_name = adapter;
}

const char *ethercat_adapter() {
    return ethercat_adapter_name.c_str();
}
