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

#ifndef cwlang_value_h
#define cwlang_value_h

#include <map>
#include <string>
#include <iostream>
#include <list>

class MachineInstance;


class Value {
public:
    enum Kind { t_empty, t_integer, t_string, t_bool, t_symbol, t_dynamic /*, t_list, t_map */};

	typedef std::list<Value*> List;
//    typedef std::map<std::string, Value> Map;

    Value() : kind(t_empty), cached_machine(0), list(0) { }
    Value(Kind k) : kind(k), cached_machine(0), list(0) { }
    virtual ~Value() { }
    Value(bool v) : kind(t_bool), bValue(v), cached_machine(0), list(0) { }
    Value(long v) : kind(t_integer), iValue(v), cached_machine(0), list(0) { }
    Value(int v) : kind(t_integer), iValue(v), cached_machine(0), list(0) { }
    Value(unsigned int v) : kind(t_integer), iValue(v), cached_machine(0), list(0) { }
    Value(unsigned long v) : kind(t_integer), iValue(v), cached_machine(0), list(0) { }
    Value(const char *str, Kind k = t_symbol) : kind(k), sValue(str), cached_machine(0), list(0) { }
    Value(std::string str, Kind k = t_symbol) : kind(k), sValue(str), cached_machine(0), list(0) { }
    Value(const Value&other);
    void push_back(Value *value) { if (list) list->push_back(value); }
    std::string asString() const;
	bool asInteger(long &val) const;
//	Value operator[](int index);
//	Value operator[](std::string index);
    virtual const Value *operator()() const { return this; }
    virtual Value *operator()() { return this; }

    Kind kind;
    bool bValue;
    long iValue;
    std::string sValue; // used for strings and for symbols
    MachineInstance *cached_machine;

    List *list;
//    Map mapValue;
    Value operator=(const Value &orig);
    std::ostream &operator<<(std::ostream &out) const;
#if 0
    void addItem(int next_value);
    void addItem(const char *next_value);
    void addItem(Value next_value);
    void addItem(std::string key, Value next_value);
#endif
    
    bool operator>=(const Value &other) const;
    bool operator<=(const Value &other) const;
	bool operator<(const Value &other) const { return !operator>=(other); }
	bool operator>(const Value &other) const { return !operator<=(other); }
    bool operator==(const Value &other) const;
    bool operator!=(const Value &other) const;
    bool operator&&(const Value &other) const;
    bool operator||(const Value &other) const;
	bool operator!() const;
	Value operator -(void) const;
	Value &operator +(const Value &other);
	Value &operator -(const Value &other);
	Value &operator *(const Value &other);
	Value &operator /(const Value &other);
	Value &operator %(const Value &other);
	Value &operator &(const Value &other);
	Value &operator |(const Value &other);
	Value &operator ^(const Value &other);
	Value &operator ~();
private:
    std::string name() const;
};

std::ostream &operator<<(std::ostream &out, const Value &val);

class DynamicValue : public Value {
public:
    DynamicValue() : Value(t_dynamic) { }
    virtual ~DynamicValue() {}
    virtual Value *operator()();
    virtual const Value *operator()() const;
};

class AnyInValue : public DynamicValue {
public:
    AnyInValue(const char *state_str, const char *list) : state(state_str), machine_list(list)  { }
    virtual ~AnyInValue() {}
    virtual Value *operator()();
    virtual const Value *operator()() const;
private:
    std::string state;
    std::string machine_list;
};

class AllInValue : public DynamicValue {
public:
    AllInValue(const char *state_str, const char *list) : state(state_str), machine_list(list) {}
    virtual ~AllInValue() {}
    virtual Value *operator()();
    virtual const Value *operator()() const;
private:
    std::string state;
    std::string machine_list;
};


class CountValue : public DynamicValue {
public:
    CountValue(const char *state_str, const char *list) : state(state_str), machine_list(list)  { }
    virtual ~CountValue() {}
    virtual Value *operator()();
    virtual const Value *operator()() const;
private:
    std::string state;
    std::string machine_list;
};

class IncludesValue : public DynamicValue {
public:
    IncludesValue(const char *state_str, const char *list) : state(state_str), machine_list(list)  { }
    virtual ~IncludesValue() {}
    virtual Value *operator()();
    virtual const Value *operator()() const;
private:
    std::string state;
    std::string machine_list;
};

class BitsetValue : public DynamicValue {
public:
    BitsetValue(const char *state_str, const char *list) : state(state_str), machine_list(list)  { }
    virtual ~BitsetValue() {}
    virtual Value *operator()();
    virtual const Value *operator()() const;
private:
    std::string state;
    std::string machine_list;
};


#endif
