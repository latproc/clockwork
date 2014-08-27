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
class DynamicValue;

uint64_t microsecs();

class Value {
public:
    enum Kind { t_empty, t_integer, t_string, t_bool, t_symbol, t_dynamic /*, t_list, t_map */};

	typedef std::list<Value*> List;
//    typedef std::map<std::string, Value> Map;

    Value();
    Value(Kind k);
    Value(bool v);
    Value(long v);
    Value(int v) ;
    Value(unsigned int v);
    Value(unsigned long v);
    Value(const char *str, Kind k = t_symbol);
    Value(std::string str, Kind k = t_symbol);
    Value(const Value&other);
    Value(DynamicValue *dv);
    Value(DynamicValue &dv);
    virtual ~Value();
    std::string asString() const;
    std::string quoted() const;
	bool asInteger(long &val) const;
    explicit operator long() { return iValue; }
    explicit operator int() { return (int)iValue; }
//	Value operator[](int index);
//	Value operator[](std::string index);

    Kind kind;
    bool bValue;
    long iValue;
    std::string sValue; // used for strings and for symbols
    MachineInstance *cached_machine;
    DynamicValue *dyn_value;
    Value *cached_value;
    int token_id;
    
    DynamicValue *dynamicValue() const { return dyn_value; }
    void setDynamicValue(DynamicValue *dv);
    void setDynamicValue(DynamicValue &dv);
    
    Value &operator=(const Value &orig);
    Value &operator=(bool);
    Value &operator=(int);
    Value &operator=(long);
    Value &operator=(unsigned long);
    Value &operator=(const char *);
    Value &operator=(std::string);

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

    std::ostream &operator<<(std::ostream &out) const;
private:
    std::string name() const;
};

std::ostream &operator<<(std::ostream &out, const Value &val);

class ListValue : public Value {
public:
    List *list;
    //    Map mapValue;
    void addItem(int next_value);
    void addItem(const char *next_value);
    void addItem(Value next_value);
    void addItem(std::string key, Value next_value);
    void push_back(Value *value) { if (list) list->push_back(value); }
};

#endif
