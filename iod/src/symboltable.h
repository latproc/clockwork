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

#ifndef cwlang_symboltable_h
#define cwlang_symboltable_h

#include <map>
#include <string>
#include <iostream>
#include <list>
#include <set>
#include "value.h"

class MachineInstance;

typedef std::pair<std::string, Value> SymbolTableNode;
typedef std::map<std::string, Value>::iterator SymbolTableIterator;
typedef std::map<std::string, Value>::const_iterator SymbolTableConstIterator;

typedef std::pair<int, Value> TokenTableNode;
typedef std::map<int, Value>::iterator TokenTableIterator;
typedef std::map<int, Value>::const_iterator TokenTableConstIterator;

class Tokeniser {
public:
    static Tokeniser* instance();
    int getTokenId(const char *name);
    int getTokenId(const std::string &);
private:
    static Tokeniser *_instance;
    std::map<std::string,int> tokens;
    int next;
    Tokeniser() : next(0) { }
};

class ClockworkToken {
public:
    static int tokCONDITION;
    static int CONSTANT;
    static int EXTERNAL;
    static int tokITEM;
    static int LIST;
    static int POINT;
    static int REFERENCE;
    static int TIMER;
    static int VARIABLE;
    static int MQTTPUBLISHER;
	static int MQTTSUBSCRIBER;
    static int POLLING_DELAY;
    static int CYCLE_DELAY;
    static int SYSTEMSETTINGS;
    static int tokVALUE;
    static int tokMessage;
    static int TRACEABLE;
    static int on;
    static int off;
	static int TRACE;
	static int DEBUG;
};

class SymbolTable {
public: 
	SymbolTable();
	enum ReplaceMode { ST_REPLACE, NO_REPLACE };
	
    bool add(const char *name, const Value &val, ReplaceMode replace_mode = ST_REPLACE);
    bool add(const std::string name, const Value &val, ReplaceMode replace_mode = ST_REPLACE);
    void add(const SymbolTable &orig, ReplaceMode replace_mode = ST_REPLACE); // load symbols optionally with replacement
    const Value &lookup(const Value &name);
    const Value &lookup(const char *name);
	const Value &find(const char *name) const;
	Value &find(const char *name);
	size_t count(const char *name) { return st.count(name); }
    bool exists(const char *name);
    bool exists(int token_id);
    void clear();
    
    SymbolTableConstIterator begin() const {return st.begin(); }
    SymbolTableConstIterator end() const { return st.end(); }
    void push_back(std::pair<std::string, Value>item) { st[item.first] = item.second; }
    
    SymbolTable(const SymbolTable &orig);
    SymbolTable &operator=(const SymbolTable &orig);
    
    std::ostream &operator <<(std::ostream & out) const;
    size_t size() const { return st.size(); }
	bool empty() const { return st.empty(); }
    
    static bool isKeyword(const Value &name);
    static bool isKeyword(const char *name);
    static const Value &getKeyValue(const char *name);
    
	static const Value Null;
	static const Value True;
	static const Value False;
	static const Value Zero;
private:
    std::map<std::string, Value>st;
    std::map<int, Value>stok;
    static SymbolTable *keywords;
    static std::set<std::string> *reserved;
    static bool initialised;
    static std::list<SymbolTable *>symbol_tables;
};

std::ostream &operator <<(std::ostream &out, const SymbolTable &st);

#endif
