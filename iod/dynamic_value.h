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
    virtual std::ostream &operator<<(std::ostream &) const;
    Value *lastResult() { return &last_result; }
protected:
    Value last_result;
};
std::ostream &operator<<(std::ostream &, const DynamicValue &);

class AssignmentValue : public DynamicValue {
public:
    AssignmentValue(const char *dest, Value *val)
    : src(*val), dest_name(dest)  { }
    virtual ~AssignmentValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    Value src;
    std::string dest_name;
};


class AnyInValue : public DynamicValue {
public:
    AnyInValue(const char *state_str, const char *list) : state(state_str), machine_list_name(list), machine_list(0)  { }
    virtual ~AnyInValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    std::string state;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class AllInValue : public DynamicValue {
public:
    AllInValue(const char *state_str, const char *list) : state(state_str), machine_list_name(list), machine_list(0) {}
    virtual ~AllInValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    std::string state;
    std::string machine_list_name;
    MachineInstance *machine_list;
};


class CountValue : public DynamicValue {
public:
    CountValue(const char *state_str, const char *list) : state(state_str), machine_list_name(list), machine_list(0)  { }
    virtual ~CountValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    std::string state;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class IncludesValue : public DynamicValue {
public:
    IncludesValue(const char *entry_str, const char *list) : entry_name(entry_str), machine_list_name(list), machine_list(0)  { }
    virtual ~IncludesValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    std::string entry_name;
    std::string machine_list_name;
    MachineInstance *machine_list;
};


class ItemAtPosValue : public DynamicValue {
public:
    ItemAtPosValue(const char *list, Value *val, bool remove = true)
        : index(*val), machine_list_name(list), machine_list(0), remove_from_list(remove)  { }
    virtual ~ItemAtPosValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    Value index;
    std::string machine_list_name;
    MachineInstance *machine_list;
    bool remove_from_list = false;
};

class SizeValue : public DynamicValue {
public:
    SizeValue(const char *list) : machine_list_name(list), machine_list(0)  { }
    virtual ~SizeValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class PopListBackValue : public DynamicValue {
public:
    PopListBackValue(const char *list, bool remove = true)
        : machine_list_name(list), machine_list(0), remove_from_list(remove)  { }
    virtual ~PopListBackValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    std::string machine_list_name;
    MachineInstance *machine_list;
    bool remove_from_list;
};

class PopListFrontValue : public DynamicValue {
public:
    PopListFrontValue(const char *list, bool remove = true)
        : machine_list_name(list), machine_list(0), remove_from_list(remove)  { }
    virtual ~PopListFrontValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    std::string machine_list_name;
    MachineInstance *machine_list;
    bool remove_from_list;
};


class BitsetValue : public DynamicValue {
public:
    BitsetValue(const char *state_str, const char *list) : state(state_str), machine_list_name(list), machine_list(0)  { }
    virtual ~BitsetValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
   
private:
    std::string state;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class EnabledValue : public DynamicValue {
public:
    EnabledValue(const char *name) : machine_name(name), machine(0) { }
    virtual ~EnabledValue() { }
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    std::string machine_name;
    MachineInstance *machine;
};

class DisabledValue : public DynamicValue {
public:
    DisabledValue(const char *name) : machine_name(name), machine(0) { }
    virtual ~DisabledValue() { }
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    
private:
    std::string machine_name;
    MachineInstance *machine;
};

#endif
