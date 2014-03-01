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

#include <iostream>
#include <iterator>
#include "symboltable.h"
#include <numeric>
#include <functional>
#include <sstream>
#include "Logger.h"
#include <boost/foreach.hpp>
#include <utility>
#include "DebugExtra.h"

Value SymbolTable::Null;
Value SymbolTable::True(true);
Value SymbolTable::False(false);
Value SymbolTable::Zero(0);

bool SymbolTable::initialised = false;
SymbolTable *SymbolTable::keywords = 0;
std::set<std::string> *SymbolTable::reserved = 0;

Tokeniser* Tokeniser::_instance = 0;

int ClockworkToken::EXTERNAL;
int ClockworkToken::POINT;
int ClockworkToken::LIST;
int ClockworkToken::TIMER;
int ClockworkToken::REFERENCE;
int ClockworkToken::tokITEM;
int ClockworkToken::VARIABLE;
int ClockworkToken::CONSTANT;
int ClockworkToken::tokCONDITION;
int ClockworkToken::PUBLISHER;
int ClockworkToken::POLLING_DELAY;
int ClockworkToken::CYCLE_DELAY;
int ClockworkToken::SYSTEMSETTINGS;
int ClockworkToken::tokVALUE;
int ClockworkToken::tokMessage;
int ClockworkToken::TRACEABLE;

Tokeniser* Tokeniser::instance() {
    if (!_instance) {
        _instance = new Tokeniser();
        ClockworkToken::EXTERNAL = _instance->getTokenId("EXTERNAL");
        ClockworkToken::POINT = _instance->getTokenId("POINT");
        ClockworkToken::LIST = _instance->getTokenId("LIST");
        ClockworkToken::TIMER = _instance->getTokenId("TIMER");
        ClockworkToken::REFERENCE = _instance->getTokenId("REFERENCE");
        ClockworkToken::tokITEM = _instance->getTokenId("ITEM");
        ClockworkToken::VARIABLE = _instance->getTokenId("VARIABLE");
        ClockworkToken::CONSTANT = _instance->getTokenId("CONSTANT");
        ClockworkToken::tokCONDITION = _instance->getTokenId("CONDITION");
        ClockworkToken::PUBLISHER = _instance->getTokenId("PUBLISHER");
        ClockworkToken::POLLING_DELAY = _instance->getTokenId("POLLING_DELAY");
        ClockworkToken::CYCLE_DELAY = _instance->getTokenId("CYCLE_DELAY");
        ClockworkToken::tokMessage = _instance->getTokenId("message");
        ClockworkToken::SYSTEMSETTINGS = _instance->getTokenId("SYSTEMSETTINGS");
        ClockworkToken::TRACEABLE = _instance->getTokenId("TRACEABLE");
        ClockworkToken::tokVALUE = _instance->getTokenId("VALUE");
    }
    return _instance;
}

int Tokeniser::getTokenId(const char *name) {
    std::map<std::string, int>::iterator found = tokens.find(name);
    if (found != tokens.end()) {
        return (*found).second;
    }
    tokens[name] = ++next;
    return next;
}

int Tokeniser::getTokenId(const std::string &name) {
    std::map<std::string, int>::iterator found = tokens.find(name);
    if (found != tokens.end()) {
        return (*found).second;
    }
    tokens[name] = ++next;
    return next;
}


SymbolTable::SymbolTable() {
    if (!initialised) {
        initialised = true;
        keywords = new SymbolTable();
        keywords->add("NOW", 0L);
        keywords->add("SECONDS", 0L);
        keywords->add("MINUTE", 0L);
        keywords->add("HOUR", 0L);
        keywords->add("DAY", 0L);
        keywords->add("MONTH", 0L);
        keywords->add("YR", 0L);
        keywords->add("YEAR", 0L);
        keywords->add("TIMEZONE", Value("",Value::t_string));
        keywords->add("TIMESTAMP", Value("",Value::t_string));
        reserved = new std::set<std::string>;
        reserved->insert("NAME");
        reserved->insert("PERSISTENT");
        reserved->insert("tab");
        reserved->insert("STATE");
        reserved->insert("PERSISTENT");
    }
}

SymbolTable::SymbolTable(const SymbolTable &orig) {
    SymbolTableConstIterator iter = orig.st.begin();
    while (iter != orig.st.end()) {
        st[(*iter).first] = (*iter).second;
        iter++;
    }
}

SymbolTable &SymbolTable::operator=(const SymbolTable &orig) {
    st.erase(st.begin(), st.end());
    SymbolTableConstIterator iter = orig.st.begin();
    while (iter != orig.st.end()) {
        st[(*iter).first] = (*iter).second;
        iter++;
    }
    return *this;
}

static bool isKeyword(const Value &name) {
    if (name.kind == Value::t_symbol || name.kind == Value::t_string)
        return isKeyword(name.sValue.c_str());
    return false;
}

/* Symbol table key values, calculated every time they are referenced */
bool SymbolTable::isKeyword(const char *name)
{
    if (!name) return false;
    return keywords->exists(name);
}

Value &SymbolTable::getKeyValue(const char *name) {
    if (!name) return Null;
    if (keywords->exists(name)) {
        Value &res = keywords->lookup(name);
        if (strcmp("NOW", name) == 0) {
            struct timeval now;
            gettimeofday(&now,0);
            unsigned long msecs = (now.tv_sec % 1000) * 1000 + (now.tv_usec + 500) / 1000;
            res = msecs;
            return res;
        }
        // the remaining values are all time fields
        static time_t last = 0L;
        time_t now = time(0);
        struct tm lt;
        localtime_r(&now, &lt);
        if (strcmp("SECONDS", name) == 0) {
            res = lt.tm_sec;
            return res;
        }
        if (strcmp("MINUTE", name) == 0) {
            res = lt.tm_min;
            return res;
        }
        if (strcmp("HOUR", name) == 0) {
            res = lt.tm_hour;
            return res;
        }
        if (strcmp("DAY", name) == 0) {
            res = lt.tm_mday;
            return res;
        }
        if (strcmp("MONTH", name) == 0) {
            res = lt.tm_mon+1;
            return res;
        }
        if (strcmp("YEAR", name) == 0) {
            res = lt.tm_year + 1900;
            return res;
        }
        if (strcmp("YR", name) == 0) {
            res = lt.tm_year - 100;
            return res;
        }
        if (strcmp("TIMEZONE", name) == 0) {
            res = lt.tm_zone;
            return res;
        }
        if (strcmp("TIMESTAMP", name) == 0) {
            char buf[40];
            ctime_r(&now, buf);
            size_t n = strlen(buf);
            if (n>1 && buf[n-1] == '\n') buf[n-1] = 0;
            res = buf;
            return res;
        }
        return res;
    }
    return Null;
}



bool SymbolTable::add(const char *name, Value val, ReplaceMode replace_mode) {
    if (replace_mode == ST_REPLACE || (replace_mode == NO_REPLACE && st.find(name) == st.end())) {
        std::string s(name);
        st[s] = val;
		return true;
    }
    else {
        //std::cerr << "Error: " << name << " already defined\n";
		return false;
    }
}

bool SymbolTable::add(const std::string name, Value val, ReplaceMode replace_mode) {
    if (replace_mode == ST_REPLACE || (replace_mode == NO_REPLACE && st.find(name) == st.end())) {
    	st[name] = val;
		return true;
	}
	else {
        //std::cerr << "Error: " << name << " already defined\n";
		return false;
	}
}

void SymbolTable::add(const SymbolTable &orig, ReplaceMode replace_mode) {
    SymbolTableConstIterator iter = orig.st.begin();
    while (iter != orig.st.end()) {
        if (replace_mode != ST_REPLACE && st.find((*iter).first) != orig.st.end()) {  // skip entries we already have if not replace mode
            ++iter; continue;
        }
        const std::string &name = (*iter).first;
        if(name != "NAME" && name != "STATE") { // these reserved words cannot be replaces en mass
            st[(*iter).first] = (*iter).second;
        }
        iter++;
    }
}

bool SymbolTable::exists(const char *name) {
    return st.find(name) != st.end();
}

Value &SymbolTable::lookup(const char *name) {
    if (this != keywords) {
        Value &res = keywords->lookup(name);
        if (res != SymbolTable::Null) return res;
    }
    SymbolTableIterator iter = st.find(name);
    if (iter != st.end()) return (*iter).second;
    return SymbolTable::Null;
}

#if 0
void Value::addItem(Value next_value) {
    if (kind == t_empty) {
        *this = next_value;
    }
    else {
        if (kind == t_integer)
            listValue.push_front(Value(iValue));
        else if (kind == t_string)
            listValue.push_front(Value(sValue.c_str()));
        
        kind = t_list;
        listValue.push_front(next_value);
    }
}

void Value::addItem(int next_value) {
    addItem(Value(next_value));
}

void Value::addItem(const char *next_value) {
    addItem(Value(next_value));
}

void Value::addItem(std::string key, Value val) {
    if (kind != t_map) return;
    mapValue[key] = val;
}
#endif

void SymbolTable::clear() {
    st.clear();
}

std::ostream &SymbolTable::operator <<(std::ostream & out) const {
    SymbolTableConstIterator iter = st.begin();
    const char *delim = "";
    size_t column = 1;
    while (iter != st.end()) {
        size_t len = strlen(delim) + (*iter).first.length() + (*iter).second.asString().length()+1;
        out << delim;
        if (column >= 100) { out << "\n    "; column = 5;}
        column += len;
        out << (*iter).first << ':' << (*iter).second;
        delim = ", ";
        iter++;
    }
    return out;
}

std::ostream &operator <<( std::ostream &out, const SymbolTable &st) {
    return st.operator<<(out);
}

