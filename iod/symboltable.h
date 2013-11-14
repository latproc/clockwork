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
#include "value.h"

class MachineInstance;

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
	size_t count(const char *name) { return st.count(name); }
    bool exists(const char *name);
    void clear();
    
    SymbolTableConstIterator begin() const {return st.begin(); }
    SymbolTableConstIterator end() const { return st.end(); }
    void push_back(std::pair<std::string, Value>item) { st[item.first] = item.second; }
    
    SymbolTable(const SymbolTable &orig);
    SymbolTable &operator=(const SymbolTable &orig);
    
    std::ostream &operator <<(std::ostream & out) const;
    size_t size() const { return st.size(); }
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
