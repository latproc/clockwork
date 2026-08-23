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
void set_parse_only(int trueOrFalse);
int parse_only();

/* cw only: MQTT is off unless --mqtt / --enable-mqtt. iod-elc ignores this. */
void set_mqtt_enabled(int trueOrFalse);
int mqtt_enabled();

void set_help_only(int trueOrFalse);
int help_only();
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

/** Clockwork process-data delivery period (µs). Independent of bus CYCLE_DELAY. */
unsigned long get_polling_time();
void set_polling_time(unsigned long pt);

bool export_to_c();
void set_export_to_c(bool c_export);

int cpu_affinity(const char *thread_name);
void set_cpu_affinity(const char *thread_name, int cpu);
int thread_rt_priority(const char *thread_name);
void set_thread_rt_priority(const char *thread_name, int priority);

bool fix_invalid_transitions();
void set_fix_invalid_transitions(bool which);

void set_ethercat_adapter(const char *adapter);
const char *ethercat_adapter();

/**
 * CLI ordered setup recipes (elc_sdo format). Repeatable.
 * Each --setup-recipe starts a new entry; following --setup-positions /
 * --setup-domain / --setup-product / --setup-vendor attach to the last entry.
 * Plant ECSETUPRECIPE machines are also discovered; CLI is an extra source.
 */
void elc_setup_recipe_add(const char *path);
void elc_setup_recipe_set_last_positions(const char *list);
void elc_setup_recipe_set_last_domain(unsigned long domain_id);
void elc_setup_recipe_set_last_product(unsigned long product_code);
void elc_setup_recipe_set_last_vendor(unsigned long vendor_id);
unsigned elc_setup_recipe_count(void);
const char *elc_setup_recipe_path_at(unsigned i);
const char *elc_setup_recipe_positions_at(unsigned i);
unsigned long elc_setup_recipe_domain_at(unsigned i);
unsigned long elc_setup_recipe_product_at(unsigned i);
unsigned long elc_setup_recipe_vendor_at(unsigned i);

#ifdef __cplusplus
}
#endif

#endif
