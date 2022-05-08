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

#ifndef cwlang_options_h
#define cwlang_options_h

#ifdef __cplusplus
extern "C" {
#endif

void set_verbose(int trueOrFalse);
int verbose();
void set_test_only(int trueOrFalse);
int test_only();
void set_persistent_store(const char *name);
const char *persistent_store();

void set_modbus_map(const char *name);
const char *modbus_map();

void set_debug_config(const char *name);
const char *debug_config();

void set_dependency_graph(const char *name);
const char *dependency_graph();

void set_graph_root(const char *root);
const char *graph_root();

void set_publisher_port(int port, bool required = false);
int publisher_port();
bool publisher_port_fixed();

void set_persistent_store_port(int port);
int persistent_store_port();

void set_modbus_port(int port);
int modbus_port();

void set_command_port(int port, bool required = false);
int command_port();
bool command_port_fixed();

void enable_statistics(bool which);
int keep_statistics();

const char *device_name();
void set_device_name(const char *new_name);

void enable_tracing(bool which);
bool tracing();

unsigned long get_cycle_time();
void set_cycle_time(unsigned long ct);

bool export_to_c();
void set_export_to_c(bool c_export);

int cpu_affinity(const char *thread_name);
void set_cpu_affinity(const char *thread_name, int cpu);

bool fix_invalid_transitions();
void set_fix_invalid_transitions(bool which);

#ifdef __cplusplus
}
#endif

#endif
