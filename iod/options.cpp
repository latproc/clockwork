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

static int opt_verbose = 0;
static int opt_test_only = 0;
const char *persistent_store_name  = 0;
const char *modbus_map_name = 0;
const char *debug_config_name = 0;
const char *dependency_graph_name = 0;

void set_verbose(int trueOrFalse)
{
	opt_verbose = trueOrFalse;
}

int verbose()
{
	return opt_verbose;
}

void set_test_only(int trueOrFalse) {
	opt_test_only = trueOrFalse;
}

int test_only() {
	return opt_test_only;
}

void set_persistent_store(const char *name) { 
	persistent_store_name = name; 
}

const char *persistent_store() { 
	return persistent_store_name; 
}

void set_modbus_map(const char *name) {
	modbus_map_name = name;
}

const char *modbus_map() {
	return modbus_map_name;
}

void set_debug_config(const char *name) {
	debug_config_name = name; 
}

const char *debug_config() {
	return debug_config_name;
}

void set_dependency_graph(const char *name) {
    dependency_graph_name = name;
}

const char *dependency_graph() {
    return dependency_graph_name;
}

