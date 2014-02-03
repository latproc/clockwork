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
    DynamicValue() :scope(0), refs(0){ }
    virtual ~DynamicValue()  {}
    virtual Value operator()(MachineInstance *scope); // uses the provided machine's scope
    virtual Value operator()(); // uses the current scope for evaluation
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    Value *lastResult() { return &last_result; }
    void setScope(MachineInstance *m) { scope = m; }
    MachineInstance *getScope() { return scope; }
    static DynamicValue *ref(DynamicValue*dv) { if (!dv) return 0; else dv->refs++; return dv; }
    DynamicValue *deref() { --refs; if (!refs) {delete this; return 0;} else return this; }
protected:
    Value last_result;
    MachineInstance *scope;
    int refs;
    DynamicValue(const DynamicValue &other) : last_result(other.last_result), scope(0), refs(1) { }
private:
    DynamicValue &operator=(const DynamicValue &other);
};
std::ostream &operator<<(std::ostream &, const DynamicValue &);

class AssignmentValue : public DynamicValue {
public:
    AssignmentValue(const char *dest, Value *val)
    : src(*val), dest_name(dest)  { }
    virtual ~AssignmentValue() {}
    virtual Value operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    AssignmentValue(const AssignmentValue &);
    
private:
    AssignmentValue(const DynamicValue &);
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
    AnyInValue(const AnyInValue &);
    
private:
    AnyInValue(const DynamicValue &);
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
    AllInValue(const AllInValue &);
    
private:
    AllInValue(const DynamicValue &);
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
    CountValue(const CountValue &);
    
private:
    CountValue(const DynamicValue &);
    std::string state;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class IncludesValue : public DynamicValue {
public:
    IncludesValue(Value val, const char *list) : entry(val), machine_list_name(list), machine_list(0)  { }
    virtual ~IncludesValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    IncludesValue(const IncludesValue &);
    
private:
    IncludesValue(const DynamicValue &);
    Value entry;
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
    ItemAtPosValue(const ItemAtPosValue &);
    
private:
    ItemAtPosValue(const DynamicValue &);
    Value index;
    std::string machine_list_name;
    MachineInstance *machine_list;
    bool remove_from_list;
};

class SizeValue : public DynamicValue {
public:
    SizeValue(const char *list) : machine_list_name(list), machine_list(0)  { }
    virtual ~SizeValue() {}
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    SizeValue(const SizeValue &);
    
private:
    SizeValue(const DynamicValue &);
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
    PopListBackValue(const PopListBackValue &);
    
private:
    PopListBackValue(const DynamicValue &);
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
    PopListFrontValue(const PopListFrontValue &);
    
private:
    PopListFrontValue(const DynamicValue &);
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
    BitsetValue(const BitsetValue &);
   
private:
    BitsetValue(const DynamicValue &);
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
    EnabledValue(const EnabledValue &);
    
private:
    EnabledValue(const DynamicValue &);
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
    DisabledValue(const DisabledValue &);
    
private:
    DisabledValue(const DynamicValue &);
    std::string machine_name;
    MachineInstance *machine;
};


class CastValue : public DynamicValue {
public:
    CastValue(const char *name, const char *type_name) : property(name), kind(type_name) { }
    virtual ~CastValue() { }
    virtual Value operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    CastValue(const CastValue &);
    
private:
    CastValue(const DynamicValue &);
    std::string property;
    std::string kind;
};



#endif
