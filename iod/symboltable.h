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

#ifndef latprocc_symboltable_h
#define latprocc_symboltable_h

#include <map>
#include <string>
#include <iostream>
#include <list>
#include <vector>

class MachineInstance;

class Value {
public:
    enum Kind { t_empty, t_integer, t_string, t_bool, t_symbol /*, t_list, t_map */};

//	typedef std::list<Value> List;
//    typedef std::map<std::string, Value> Map;

    Value() : kind(t_empty), cached_machine(0) { }
    Value(bool v) : kind(t_bool), bValue(v), cached_machine(0) { }
    Value(long v) : kind(t_integer), iValue(v), cached_machine(0) { }
    Value(int v) : kind(t_integer), iValue(v), cached_machine(0) { }
    Value(unsigned int v) : kind(t_integer), iValue(v), cached_machine(0) { }
    Value(unsigned long v) : kind(t_integer), iValue(v), cached_machine(0) { }
    Value(const char *str, Kind k = t_symbol) : kind(k), sValue(str), cached_machine(0) { }
    Value(std::string str, Kind k = t_symbol) : kind(k), sValue(str), cached_machine(0) { }
    Value(const Value&other);
//    void push_back(int value) { listValue.push_back(value); }
    std::string asString() const;
	bool asInteger(long &val) const;
//	Value operator[](int index);
//	Value operator[](std::string index);

    Kind kind;
    bool bValue;
    long iValue;
    std::string sValue; // used for strings and for symbols
    MachineInstance *cached_machine;

//    List listValue;
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

typedef std::pair<std::string, Value> SymbolTableNode;
typedef std::map<std::string, Value>::iterator SymbolTableIterator;
typedef std::map<std::string, Value>::const_iterator SymbolTableConstIterator;

class SymbolTable {
public: 
    SymbolTable();
	enum ReplaceMode { ST_REPLACE, NO_REPLACE };
	
    bool add(const char *name, Value val, ReplaceMode replace_mode = ST_REPLACE);
    bool add(const std::string name, Value val, ReplaceMode replace_mode = ST_REPLACE);
    Value &lookup(const char *name);
	int count(const char *name) { return st.count(name); }
    bool exists(const char *name);
    void clear();
    
    SymbolTableConstIterator begin() const {return st.begin(); }
    SymbolTableConstIterator end() const { return st.end(); }
    void push_back(std::pair<std::string, Value>item) { st[item.first] = item.second; }
    
    SymbolTable(const SymbolTable &orig);
    SymbolTable &operator=(const SymbolTable &orig);
    
    std::ostream &operator <<(std::ostream & out) const;
    int size() const { return st.size(); }
	bool empty() const { return st.empty(); }
    
    static bool isKeyword(const char *name);
    static Value &getKeyValue(const char *name);
    
	static Value Null;
	static Value True;
	static Value False;
private:
    std::map<std::string, Value>st;
    static SymbolTable *keywords;
    static bool initialised;
    static std::list<SymbolTable *>symbol_tables;
};

std::ostream &operator <<(std::ostream &out, const SymbolTable &st);

#endif
