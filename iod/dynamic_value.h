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

#ifndef cwlang_dynamic_value_h
#define cwlang_dynamic_value_h

#include <map>
#include <string>
#include <iostream>
#include <list>
#include "value.h"

class MachineInstance;
class DynamicValue {
public:
    DynamicValue() { }
    virtual ~DynamicValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
};

class AnyInValue : public DynamicValue {
public:
    AnyInValue(const char *state_str, const char *list) : state(state_str), machine_list(list)  { }
    virtual ~AnyInValue() {}
    virtual Value operator()();
    virtual DynamicValue *clone() const;
    
private:
    std::string state;
    std::string machine_list;
};

class AllInValue : public DynamicValue {
public:
    AllInValue(const char *state_str, const char *list) : state(state_str), machine_list(list) {}
    virtual ~AllInValue() {}
    virtual Value operator()();
    virtual DynamicValue *clone() const;
    
private:
    std::string state;
    std::string machine_list;
};


class CountValue : public DynamicValue {
public:
    CountValue(const char *state_str, const char *list) : state(state_str), machine_list(list)  { }
    virtual ~CountValue() {}
    virtual Value operator()();
    virtual DynamicValue *clone() const;
    
private:
    std::string state;
    std::string machine_list;
};

class IncludesValue : public DynamicValue {
public:
    IncludesValue(const char *entry_str, const char *list) : entry_name(entry_str), machine_list_name(list), machine_list(0)  { }
    virtual ~IncludesValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    
private:
    virtual Value operator()();

    std::string entry_name;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class BitsetValue : public DynamicValue {
public:
    BitsetValue(const char *state_str, const char *list) : state(state_str), machine_list(list)  { }
    virtual ~BitsetValue() {}
    virtual Value operator()();
    virtual DynamicValue *clone() const;
    
private:
    std::string state;
    std::string machine_list;
};


#endif
