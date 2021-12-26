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

#ifndef __IOAddress__h_
#define __IOAddress__h_

#include "MQTTInterface.h"
#include "State.h"
#include "filtering.h"
#include <boost/thread/mutex.hpp>
#include <lib_clockwork_client/includes.hpp>
#include <list>
#include <map>
#include <ostream>
#include <set>
#include <string>

struct IOAddress {
    unsigned int module_position;
    unsigned int io_offset;
    int io_bitpos;
    int32_t value;
    unsigned int bitlen;
    unsigned int entry_position;
    bool is_signed;
    IOAddress(unsigned int module_pos, unsigned int offs, int bitp,
              unsigned int entry_pos, unsigned int len = 1,
              bool signed_value = false)
        : module_position(module_pos), io_offset(offs), io_bitpos(bitp),
          value(0), bitlen(len), entry_position(entry_pos),
          is_signed(signed_value) {}
    IOAddress(const IOAddress &other)
        : module_position(other.module_position), io_offset(other.io_offset),
          io_bitpos(other.io_bitpos), value(other.value), bitlen(other.bitlen),
          entry_position(other.entry_position), is_signed(other.is_signed),
          description(other.description) {}
    IOAddress()
        : module_position(0), io_offset(0), io_bitpos(0), value(0), bitlen(1),
          entry_position(0), is_signed(false) {}
    std::string description;
};

#endif