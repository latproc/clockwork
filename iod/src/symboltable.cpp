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

#include "symboltable.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "value.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <functional>
#include <iostream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <utility>

const Value SymbolTable::Null;
const Value SymbolTable::True(true);
const Value SymbolTable::False(false);
const Value SymbolTable::Zero(0);
Value NullValue;

bool SymbolTable::initialised = false;
SymbolTable *SymbolTable::keywords = 0;
std::set<std::string> *SymbolTable::reserved = 0;

Tokeniser *Tokeniser::_instance = 0;

int ClockworkToken::EXTERNAL;
int ClockworkToken::POINT;
int ClockworkToken::LIST;
int ClockworkToken::TIMER;
int ClockworkToken::REFERENCE;
int ClockworkToken::tokITEM;
int ClockworkToken::VARIABLE;
int ClockworkToken::CONSTANT;
int ClockworkToken::tokCONDITION;
int ClockworkToken::MQTTPUBLISHER;
int ClockworkToken::MQTTSUBSCRIBER;
int ClockworkToken::POLLING_DELAY;
int ClockworkToken::CYCLE_DELAY;
int ClockworkToken::SYSTEMSETTINGS;
int ClockworkToken::tokVALUE;
int ClockworkToken::tokMessage;
int ClockworkToken::TRACEABLE;
int ClockworkToken::on;
int ClockworkToken::off;
int ClockworkToken::DEBUG;
int ClockworkToken::TRACE;

Tokeniser *Tokeniser::instance() {
    if (!_instance) {
        _instance = new Tokeniser();
        if (_instance) {
            ClockworkToken::EXTERNAL = _instance->getTokenId("EXTERNAL");
            ClockworkToken::POINT = _instance->getTokenId("POINT");
            ClockworkToken::LIST = _instance->getTokenId("LIST");
            ClockworkToken::TIMER = _instance->getTokenId("TIMER");
            ClockworkToken::REFERENCE = _instance->getTokenId("REFERENCE");
            ClockworkToken::tokITEM = _instance->getTokenId("ITEM");
            ClockworkToken::VARIABLE = _instance->getTokenId("VARIABLE");
            ClockworkToken::CONSTANT = _instance->getTokenId("CONSTANT");
            ClockworkToken::tokCONDITION = _instance->getTokenId("CONDITION");
            ClockworkToken::MQTTPUBLISHER = _instance->getTokenId("MQTTPUBLISHER");
            ClockworkToken::MQTTSUBSCRIBER = _instance->getTokenId("MQTTSUBSCRIBER");
            ClockworkToken::POLLING_DELAY = _instance->getTokenId("POLLING_DELAY");
            ClockworkToken::CYCLE_DELAY = _instance->getTokenId("CYCLE_DELAY");
            ClockworkToken::tokMessage = _instance->getTokenId("message");
            ClockworkToken::SYSTEMSETTINGS = _instance->getTokenId("SYSTEMSETTINGS");
            ClockworkToken::TRACEABLE = _instance->getTokenId("TRACEABLE");
            ClockworkToken::tokVALUE = _instance->getTokenId("VALUE");
            ClockworkToken::on = _instance->getTokenId("on");
            ClockworkToken::off = _instance->getTokenId("off");
            ClockworkToken::DEBUG = _instance->getTokenId("DEBUG");
            ClockworkToken::TRACE = _instance->getTokenId("TRACE");
        }
    }
    return _instance;
}

int Tokeniser::getTokenId(const std::string &name) {
    std::map<std::string, int>::iterator found = tokens.find(name);
    if (found != tokens.end()) {
        return (*found).second;
    }
    int n = ++next;
    tokens[name] = n;
    return n;
}

int Tokeniser::getTokenId(const char *name) { return getTokenId(std::string(name)); }

SymbolTable::SymbolTable() {
    if (!initialised) {
        initialised = true;
        keywords = new SymbolTable();
        keywords->add("NOW", 0);
        keywords->add("CLOCK", 0);
        keywords->add("SECONDS", 0);
        keywords->add("MINUTE", 0);
        keywords->add("HOUR", 0);
        keywords->add("DAY", 0);
        keywords->add("MONTH", 0);
        keywords->add("YR", 0);
        keywords->add("YEAR", 0);
        keywords->add("TIMESEQ", "");
        keywords->add("LOCALTIME", "");
        keywords->add("UTCTIME", "");
        keywords->add("TIMEZONE", "");
        keywords->add("TIMESTAMP", "");
        keywords->add("UTCTIMESTAMP", "");
        keywords->add("ISOTIMESTAMP", "");
        keywords->add("RANDOM", "");
        reserved = new std::set<std::string>;
        reserved->insert("NAME");
        reserved->insert("PERSISTENT");
        reserved->insert("tab");
        reserved->insert("STATE");
    }
}

SymbolTable::SymbolTable(const SymbolTable &orig) {
    SymbolTableConstIterator iter = orig.st.begin();
    while (iter != orig.st.end()) {
        st[(*iter).first] = (*iter).second;
        if ((*iter).second.kind == Value::t_symbol) {
            int tok = Tokeniser::instance()->getTokenId((*iter).first.c_str());
            stok[tok] = (*iter).second;
        }
        iter++;
    }
}

SymbolTable &SymbolTable::operator=(const SymbolTable &orig) {
    st.erase(st.begin(), st.end());
    SymbolTableConstIterator iter = orig.st.begin();
    while (iter != orig.st.end()) {
        st[(*iter).first] = (*iter).second;
        if ((*iter).second.kind == Value::t_symbol) {
            int tok = Tokeniser::instance()->getTokenId((*iter).first.c_str());
            stok[tok] = (*iter).second;
        }
        iter++;
    }
    return *this;
}

bool SymbolTable::isKeyword(const Value &name) {
    if (name.kind == Value::t_symbol || name.kind == Value::t_string) {
        if (name.token_id && keywords->exists(name.token_id)) {
            return true;
        }
        return keywords->exists(name.sValue.c_str());
    }
    return false;
}

/* Symbol table key values, calculated every time they are referenced */
bool SymbolTable::isKeyword(const char *name) {
    if (!name) {
        return false;
    }
    return keywords->exists(name);
}

const Value &SymbolTable::getKeyValue(const char *name) {
    if (!name) {
        return Null;
    }
    if (keywords->exists(name)) {
        using namespace boost::posix_time;
        Value &res = keywords->find(name);
        if (strcmp("NOW", name) == 0) {
            unsigned long msecs = (microsecs() + 500) / 1000;
            res = msecs;
            return res;
        }
        else if (strcmp("CLOCK", name) == 0) {
            Value &res = keywords->find(name);
            res = (long)microsecs();
            return res;
        }
        else if (strcmp("RANDOM", name) == 0) {
            unsigned long val = random();
            res = val;
            return res;
        }
        ptime utc(microsec_clock::universal_time());
        ptime local(microsec_clock::local_time());
        struct tm lt = to_tm(local);
        if (strcmp("ISOTIMESTAMP", name) == 0) {
            char buf[40];
            std::stringstream ss;
            ss << to_iso_string(utc) << "Z" << std::flush;
            snprintf(buf, 40, "%s", ss.str().c_str());
            res = Value(buf, Value::t_string);
            return res;
        }
        // the remaining values are all time fields
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
            res = lt.tm_mon + 1;
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
            time_t now = time(0);
            localtime_r(&now, &lt);
            res = lt.tm_zone;
            return res;
        }
        if (strcmp("LOCALTIME", name) == 0) {
            char buf[40];
            const char *fmt = "%04d/%02d/%02d %02d:%02d:%02d.%03lu";
            if (sizeof(long long) == sizeof(uint64_t)) {
                fmt = "%04d/%02d/%02d %02d:%02d:%02d.%03llu";
            }
            snprintf(buf, 40, fmt, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour,
                     lt.tm_min, lt.tm_sec, local.time_of_day().total_milliseconds());
            res = Value(buf, Value::t_string);
            return res;
        }
        if (strcmp("UTCTIME", name) == 0) {
            char buf[40];
            const char *fmt = "%04d/%02d/%02d %02d:%02d:%02d.%03lu";
            if (sizeof(long long) == sizeof(uint64_t)) {
                fmt = "%04d/%02d/%02d %02d:%02d:%02d.%03llu";
            }
            lt = to_tm(utc);
            snprintf(buf, 40, fmt, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour,
                     lt.tm_min, lt.tm_sec, local.time_of_day().total_milliseconds());
            res = Value(buf, Value::t_string);
            return res;
        }
        if (strcmp("TIMESEQ", name) == 0) {
            char buf[40];
            const char *fmt = "%02d%02d%02d%02d%02d%02d%03lu";
            if (sizeof(long long) == sizeof(uint64_t)) {
                fmt = "%02d%02d%02d%02d%02d%02d%03llu";
            }
            snprintf(buf, 40, fmt, lt.tm_year - 100, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour,
                     lt.tm_min, lt.tm_sec, local.time_of_day().total_milliseconds());
            res = Value(buf, Value::t_string);
            return res;
        }
        if (strcmp("TIMESTAMP", name) == 0) {
            char buf[40];
            time_t now = time(0);
            localtime_r(&now, &lt);
            asctime_r(&lt, buf);
            size_t n = strlen(buf);
            if (n > 1 && buf[n - 1] == '\n') {
                --n;
                buf[n] = 0;
            }
            if (n + strlen(lt.tm_zone) < 39) {
                snprintf(buf + n, 39 - n, " %s", lt.tm_zone);
            }
            res = Value(buf, Value::t_string);
            return res;
        }
        if (strcmp("UTCTIMESTAMP", name) == 0) {
            char buf[40];
            tm ut;
            ut = to_tm(utc);
            asctime_r(&ut, buf);
            size_t n = strlen(buf);
            if (n > 1 && buf[n - 1] == '\n') {
                --n;
                buf[n] = 0;
            }
            res = Value(buf, Value::t_string);
            return res;
        }

        return res;
    }
    return Null;
}

bool SymbolTable::add(const char *name, const Value &val, ReplaceMode replace_mode) {
    if (replace_mode == ST_REPLACE || (replace_mode == NO_REPLACE && st.find(name) == st.end())) {
        std::string s(name);
        st[s] = val;
        if (val.kind == Value::t_symbol) {
            stok[Tokeniser::instance()->getTokenId(name)] = val;
        }
        return true;
    }
    else {
        //std::cerr << "Error: " << name << " already defined\n";
        return false;
    }
}

bool SymbolTable::add(const std::string name, const Value &val, ReplaceMode replace_mode) {
    if (replace_mode == ST_REPLACE || (replace_mode == NO_REPLACE && st.find(name) == st.end())) {
        st[name] = val;
        if (val.kind == Value::t_symbol) {
            stok[Tokeniser::instance()->getTokenId(name)] = val;
        }
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
        if (replace_mode != ST_REPLACE &&
            st.find((*iter).first) !=
                st.end()) { // skip entries we already have if not replace mode
            ++iter;
            continue;
        }
        const std::string &name = (*iter).first;
        if (name != "NAME" && name != "STATE") { // these reserved words cannot be replaced en mass
            st[(*iter).first] = (*iter).second;
            if ((*iter).second.kind == Value::t_symbol) {
                int tok = Tokeniser::instance()->getTokenId(name.c_str());
                stok[tok] = (*iter).second;
            }
        }
        iter++;
    }
}

bool SymbolTable::exists(const char *name) const { return st.find(name) != st.end(); }

bool SymbolTable::exists(int tok) const { return stok.find(tok) != stok.end(); }

const Value &SymbolTable::find(const char *name) const {
    if (this != keywords) {
        Value &res = keywords->find(name);
        if (res != SymbolTable::Null) {
            return res;
        }
    }
    SymbolTableConstIterator iter = st.find(name);
    if (iter != st.end()) {
        return (*iter).second;
    }
    return NullValue;
}

Value &SymbolTable::find(const char *name) {
    if (this != keywords) {
        Value &res = keywords->find(name);
        if (res != SymbolTable::Null) {
            return res;
        }
    }
    SymbolTableIterator iter = st.find(name);
    if (iter != st.end()) {
        return (*iter).second;
    }
    return NullValue;
}

const Value &SymbolTable::lookup(const char *name) const {
    if (this != keywords) {
        const Value &res = keywords->lookup(name);
        if (res != SymbolTable::Null) {
            return res;
        }
    }
    SymbolTableConstIterator iter = st.find(name);
    if (iter != st.end()) {
        return (*iter).second;
    }
    return SymbolTable::Null;
}

const Value &SymbolTable::lookup(const Value &name) const {
    if (this != keywords) {
        const Value &res = keywords->lookup(name);
        if (res != SymbolTable::Null) {
            return res;
        }
    }
    TokenTableConstIterator iter = stok.find(name.token_id);
    if (iter != stok.end()) {
        return (*iter).second;
    }
    return SymbolTable::Null;
}

#if 0
void Value::addItem(Value next_value)
{
    if (kind == t_empty) {
        *this = next_value;
    }
    else {
        if (kind == t_integer) {
            listValue.push_front(Value(iValue));
        }
        else if (kind == t_float) {
            listValue.push_front(Value(fValue));
        }
        else if (kind == t_string) {
            listValue.push_front(Value(sValue.c_str()));
        }

        kind = t_list;
        listValue.push_front(next_value);
    }
}

void Value::addItem(int next_value)
{
    addItem(Value(next_value));
}

void Value::addItem(const char *next_value)
{
    addItem(Value(next_value));
}

void Value::addItem(std::string key, Value val)
{
    if (kind != t_map) {
        return;
    }
    mapValue[key] = val;
}
#endif

void SymbolTable::clear() {
    st.clear();
    stok.clear();
}

std::ostream &SymbolTable::operator<<(std::ostream &out) const {
    SymbolTableConstIterator iter = st.begin();
    const char *delim = "";
    size_t column = 1;
    while (iter != st.end()) {
        size_t len =
            strlen(delim) + (*iter).first.length() + (*iter).second.asString().length() + 1;
        out << delim;
        if (column >= 100) {
            out << "\n    ";
            column = 5;
        }
        column += len;
        out << (*iter).first << ':' << (*iter).second;
        delim = ", ";
        iter++;
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const SymbolTable &st) { return st.operator<<(out); }
