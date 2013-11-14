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

Value SymbolTable::Null(-432576);
Value SymbolTable::True(true);
Value SymbolTable::False(false);

bool SymbolTable::initialised = false;
SymbolTable *SymbolTable::keywords = 0;

Value *DynamicValue::operator()() { return &SymbolTable::False; }
const Value *DynamicValue::operator()() const { return &SymbolTable::False; }

Value *AnyInValue::operator()() { return &SymbolTable::False; }
const Value *AnyInValue::operator()() const { return &SymbolTable::False; }
Value *AllInValue::operator()() { return &SymbolTable::False; }
const Value *AllInValue::operator()() const {return &SymbolTable::False; }
Value *CountValue::operator()() { return &SymbolTable::False; }
const Value *CountValue::operator()() const { return &SymbolTable::False; }
Value *IncludesValue::operator()() { return &SymbolTable::False; }
const Value *IncludesValue::operator()() const { return &SymbolTable::False; }
Value *BitsetValue::operator()() { return &SymbolTable::Null; }
const Value *BitsetValue::operator()() const { return &SymbolTable::Null; }



Value::Value(const Value&other) :kind(other.kind), bValue(other.bValue), iValue(other.iValue), sValue(other.sValue),
					cached_machine(other.cached_machine) {
//    if (kind == t_list) {
//        std::copy(other.listValue.begin(), other.listValue.end(), std::back_inserter(listValue));
//    }
//    else if (kind == t_map) {
//        std::pair<std::string, Value> node;
//        BOOST_FOREACH(node, other.mapValue) {
//            mapValue[node.first] = node.second;
//        }
//    }
}

Value Value::operator=(const Value &orig){
    kind=orig.kind;
//    listValue.erase(listValue.begin(), listValue.end());
    switch (kind) {
        case t_bool:
            bValue = orig.bValue;
            break;
        case t_integer:
            iValue = orig.iValue;
            break;
        case t_string:
        case t_symbol:
            sValue = orig.sValue;
            break;
#if 0
        case t_list:
            std::copy(orig.listValue.begin(), orig.listValue.end(), std::back_inserter(listValue));
            break;
        case t_map: {
                std::pair<std::string, Value> node;
                BOOST_FOREACH(node, orig.mapValue) {
                    mapValue[node.first] = node.second;
                }
            }
            break;
#endif
        default:
            break;
    }
    cached_machine = orig.cached_machine;
    return *this;
}

std::string Value::name() const {
    switch(kind) {
        case t_symbol:
        case t_string: return sValue; break;
#if 0
        case t_map:
        case t_list: {
            Value v = *(listValue.begin());
            return v.name();
        }
#endif
        default: ;
    }
    return "Untitled";
}

#if 0
Value Value::operator[](int index) {
	if (kind != t_list) 
		if (index == 0) return *this;
	else
		return 0;
	if (index<0 || index >= listValue.size()) return 0;
	int i=0;
	List::iterator iter = listValue.begin();
	while (iter != listValue.end() && i++<index) iter++;
	return *iter;
}
#endif

bool Value::operator>=(const Value &other) const {
    Kind a = kind;
	Kind b = other.kind;

	if (a == t_symbol) a = t_string;
	if (b == t_symbol) b = t_string;

	if (a != b && (a == t_integer || b == t_integer) && (a == t_string || b == t_string) ) {
		long x,y;
		if (asInteger(x) && other.asInteger(y)) 
			return x >= y;
		else 
			return false;
	}

	if (a != b) return false;
    switch (kind) {
        case t_empty: return false;
            break;
        case t_integer: 
			return iValue >= other.iValue;
			break;
        case t_symbol:
        case t_string: return sValue >= other.sValue;
            break;            
        default:
            break;
    }
    return false;
}

bool Value::operator<=(const Value &other) const {
    Kind a = kind;
	Kind b = other.kind;

	if (a == t_symbol) a = t_string;
	if (b == t_symbol) b = t_string;

	if (a != b && (a == t_integer || b == t_integer) && (a == t_string || b == t_string) ) {
		long x,y;
		if (asInteger(x) && other.asInteger(y)) 
			return x <= y;
		else 
			return false;
	}

	if (a != b) return false;
    switch (kind) {
        case t_empty: return false;
            break;
        case t_integer: 
			return iValue <= other.iValue;
			break;
		case t_symbol:
        case t_string: return sValue <= other.sValue;
            break;            
        default:
            break;
    }
    return false;
}

bool Value::operator==(const Value &other) const {

    Kind a = kind;
	Kind b = other.kind;

	if (a == t_symbol) a = t_string;
	if (b == t_symbol) b = t_string;

	if (a != b && (a == t_integer || b == t_integer) && (a == t_string || b == t_string) ) {
		long x,y;
		if (asInteger(x) && other.asInteger(y)) 
			return x == y;
		else 
			return false;
	}

    if (a != b) return false; // different types cannot be equal (yet)
    switch (kind) {
        case t_empty: return true;
            break;
        case t_integer: 
			return iValue == other.iValue;
			break;
		case t_symbol:
        case t_string: return sValue == other.sValue;
            break;            
        case t_bool: return bValue == other.bValue;
            break;            
        default:
            break;
    }
    return false;
}

bool Value::operator!=(const Value &other) const {
    Kind a = kind;
	Kind b = other.kind;

	if (a == t_symbol) a = t_string;
	if (b == t_symbol) b = t_string;

	if (a != b && (a == t_integer || b == t_integer) && (a == t_string || b == t_string) ) {
		long x,y;
		if (asInteger(x) && other.asInteger(y)) 
			return x != y;
		else 
			return true;
	}

	if (a != b) return true;
    switch (kind) {
        case t_empty: return false;
            break;
        case t_integer: 
			return iValue != other.iValue;
			break;
		case t_symbol:
        case t_string: return sValue != other.sValue;
            break;            
        case t_bool: return bValue != other.bValue;
            break;            
        default:
            break;
    }
    return false;
}

bool Value::operator&&(const Value &other) const {
    switch (kind) {
        case t_empty: return false;
            break;
        case t_integer: return iValue && other.iValue;
            break;
        case t_bool: return bValue && other.bValue;
            break;            
        default:
            break;
    }
    return false;
}

bool Value::operator||(const Value &other) const {
    switch (kind) {
        case t_empty: return false;
            break;
        case t_integer: return iValue || other.iValue;
            break;
        case t_bool: return bValue || other.bValue;
            break;            
        default:
            break;
    }
    return false;
}

bool Value::operator!() const {
    switch (kind) {
        case t_empty: return true;
            break;
        case t_integer: return !iValue;
            break;
        case t_bool: return !bValue;
            break; 
		case t_symbol:
		case t_string: return false;
			break;
        default:
            break;
    }
    return true;
}

static bool stringToLong(const std::string &s, long &x) {
	const char *str = s.c_str();
	char *end;
	x = strtol(str, &end, 0);
	if (*end) {
		DBG_PREDICATES << "str to long parsed " << (end-str) << " chars but did not hit the end of string '" << str << "'\n";
	}
	return *end == 0;
}

namespace ValueOperations {
	struct ValueOperation {
		virtual Value operator()(const Value &a, const Value &b) const { 
			return 0; 
		}
	};

	struct Sum : public ValueOperation {
		Value operator()(const Value &a, const Value &b) const { 
			return a.iValue + b.iValue; 
		}
	};

	struct Minus : public ValueOperation {
		Value operator()(const Value &a, const Value &b) const { 
			return a.iValue - b.iValue; 
		}
	};

	struct Multiply : public ValueOperation {
		Value operator()(const Value &a, const Value &b) const { 
			return a.iValue * b.iValue; 
		}
	};

	struct Divide : public ValueOperation {
		Value operator()(const Value &a, const Value &b) const { 
			if (b.iValue == 0) 
				return 0;
			else 
				return a.iValue / b.iValue; 
		}
	};

	struct Modulus : public ValueOperation {
		Value operator()(const Value &a, const Value &b) const { 
			if (b.iValue == 0)
				return 0;
			else
				return a.iValue % b.iValue; 
		}
	};

	struct BitAnd : public ValueOperation {
		Value operator()(const Value &a, const Value &b) const {
			return a.iValue & b.iValue;
		}
	};
    
	struct BitOr : public ValueOperation {
		Value operator()(const Value &a, const Value &b) const {
			return a.iValue | b.iValue;
		}
	};
    
	struct BitXOr : public ValueOperation {
		Value operator()(const Value &a, const Value &b) const {
			return a.iValue ^ b.iValue;
		}
	};

}
using namespace ValueOperations;

struct TypeFix {
	Value operator()(const Value &a, ValueOperation *op, const Value &b) const {
		if (a.kind != b.kind) {
			long x;
			if (a.kind == Value::t_integer && (b.kind == Value::t_string || b.kind == Value::t_symbol) ) {
				if (stringToLong(b.sValue, x))  {
					Value v(x);
					return (*op)(a, v);
				}
				else {
					DBG_PREDICATES << "Trying to add a string and value but the string does not contain a number\n";
					return a;
				}
			}
			else if (b.kind == Value::t_integer && (a.kind == Value::t_string || a.kind == Value::t_symbol) ){
				if (stringToLong(a.sValue, x)) {
					Value v(x);
					return (*op)(v, b);
				}
				else {
					//DBG_PREDICATES << "Trying to add a string and value but the string does not contain a number\n";
                    Value s(a);
					return s.operator+(b.asString());
				}
			}
			else {
				DBG_PREDICATES << " type clash, returning lhs a is" <<a.kind << " and b is " << b.kind << "\n";
				return a;
			}
		}
		DBG_PREDICATES << "invalid call to TypeFix operator when type are the same for: " << a << " and " << b << "\n";
		return a; // invalid usage
	}
};


Value &Value::operator+(const Value &other) {
	if ( ( (kind != t_symbol && kind != t_string) || (other.kind != t_symbol && other.kind != t_string) )
        && kind != other.kind) {
		Sum op;
		TypeFix tf;
		Value new_value = tf(*this, &op, other);
        if (new_value.kind == t_integer) {
            iValue = new_value.iValue;
            kind = t_integer;
        }
        else {
            sValue = new_value.asString();
            kind = t_string;
        }
		return *this;
	}
	switch(kind) {
		case t_integer: iValue += other.iValue; break;
		case t_bool: bValue |= other.bValue;
		case t_symbol:
		case t_string: sValue += other.asString();
		default: ;
	}
	return *this;
}

Value Value::operator-(void) const {
	switch(kind) {
		case t_integer: return Value(-iValue);
		case t_symbol:
		case t_string:
				long x;
				char *end;
				x = strtol(sValue.c_str(),&end,0);
				if (*end == 0) return Value(-x);
		default: ;
	}
	return *this;
}

Value &Value::operator-(const Value &other) {
	if (kind != other.kind) {
		Minus op;
		TypeFix tf;
		iValue = tf(*this, &op, other).iValue;
        kind = t_integer;
		return *this;
	}
	switch(kind) {
		case t_integer: iValue -= other.iValue; break;
		case t_bool: bValue |= other.bValue;
		default: ;
	}
	return *this;
}

Value &Value::operator*(const Value &other) {
	if (kind != other.kind) {
		Multiply op;
		TypeFix tf;
		iValue = tf(*this, &op, other).iValue;
        kind = t_integer;
		return *this;
	}
	switch(kind) {
		case t_integer: iValue *= other.iValue; break;
		case t_bool: bValue &= other.bValue;
		default: ;
	}
	return *this;
}

Value &Value::operator/(const Value &other) {
	if (kind != other.kind) {
		Divide op;
		TypeFix tf;
		iValue = tf(*this, &op, other).iValue;
        kind = t_integer;
		return *this;
	}
	switch(kind) {
		case t_integer: if (other.iValue == 0) iValue = 0; else iValue /= other.iValue; break;
		case t_bool: bValue &= other.bValue;
		default: ;
	}
	return *this;
}

Value &Value::operator%(const Value &other) {
	if (kind != other.kind) {
		Modulus op;
		TypeFix tf;
		iValue = tf(*this, &op, other).iValue;
        kind = t_integer;
		return *this;
	}
	switch(kind) {
		case t_integer: if (other.iValue == 0) iValue = 0; else iValue = iValue % other.iValue; break;
		case t_bool: bValue ^= other.bValue;
		default: ;
	}
	return *this;
}

Value &Value::operator &(const Value &other) {
	if (kind != other.kind) {
		BitAnd op;
		TypeFix tf;
		iValue = tf(*this, &op, other).iValue;
        kind = t_integer;
		return *this;
	}
	switch(kind) {
		case t_integer: iValue = iValue & other.iValue; break;
		case t_bool: bValue &= other.bValue;
		default: ;
	}
	return *this;
}

Value &Value::operator |(const Value &other) {
	if (kind != other.kind) {
		BitOr op;
		TypeFix tf;
		iValue = tf(*this, &op, other).iValue;
        kind = t_integer;
		return *this;
	}
	switch(kind) {
		case t_integer: iValue = iValue | other.iValue; break;
		case t_bool: bValue |= other.bValue;
		default: ;
	}
	return *this;
}

Value &Value::operator ^(const Value &other) {
	if (kind != other.kind) {
		BitXOr op;
		TypeFix tf;
		iValue = tf(*this, &op, other).iValue;
        kind = t_integer;
		return *this;
	}
	switch(kind) {
		case t_integer: iValue = iValue ^ other.iValue; break;
		case t_bool: bValue ^= other.bValue;
		default: ;
	}
	return *this;
}

Value &Value::operator ~() {
	switch(kind) {
		case t_integer: iValue = ~iValue; break;
		case t_bool: bValue = !bValue;
		default: ;
	}
	return *this;
}



#if 0
Value Value::operator[](std::string key) {
	if (kind != t_map) 
        return 0;
    Map::iterator iter = mapValue.find(key);
    if (iter == mapValue.end()) return 0;
    return (*iter).second;
}
#endif

std::ostream &Value::operator<<(std::ostream &out) const {
    switch(kind) {
        case t_empty: out << "(empty)"; break;
        case t_integer: out << iValue; break;
        case t_symbol: out << sValue; break;
        case t_string: out << sValue; break;
#if 0
        case t_list:   { 
            std::ostream_iterator<Value> o_iter(out, ","); 
            std::copy(listValue.begin(), listValue.end(), o_iter);
            out << "(" << listValue.size() << " values)"; 
        } 
        case t_map: {
            if (mapValue.size())
                out << "(Properties)";
        }
#endif
        case t_bool: {
            out << ((bValue) ? "true" : "false");
        }
		case t_dynamic: {
			out << "<dynamic value>";
		}
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const Value &val){ return val.operator<<(out); }

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
            res.iValue = msecs;
            return res;
        }
        // the remaining values are all time fields
        static time_t last = 0L;
        time_t now = time(0);
        struct tm lt;
        if (last != now) {
            localtime_r(&now, &lt);
        }
        if (strcmp("SECONDS", name) == 0) {
            res.iValue = lt.tm_sec;
            return res;
        }
        if (strcmp("MINUTE", name) == 0) {
            res.iValue = lt.tm_min;
            return res;
        }
        if (strcmp("HOUR", name) == 0) {
            res.iValue = lt.tm_hour;
            return res;
        }
        if (strcmp("DAY", name) == 0) {
            res.iValue = lt.tm_mday;
            return res;
        }
        if (strcmp("MONTH", name) == 0) {
            res.iValue = lt.tm_mon+1;
            return res;
        }
        if (strcmp("YEAR", name) == 0) {
            res.iValue = lt.tm_year + 1900;
            return res;
        }
        if (strcmp("YR", name) == 0) {
            res.iValue = lt.tm_year - 100;
            return res;
        }
        if (strcmp("TIMEZONE", name) == 0) {
            res.sValue = lt.tm_zone;
            return res;
        }
        if (strcmp("TIMESTAMP", name) == 0) {
            char buf[40];
            ctime_r(&now, buf);
            int n = strlen(buf);
            if (n>1 && buf[n-1] == '\n') buf[n-1] = 0;
            res.sValue = buf;
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

bool SymbolTable::exists(const char *name) {
    return st.find(name) != st.end();
}

Value &SymbolTable::lookup(const char *name) {
    if (this != keywords && keywords->exists(name))
        return keywords->lookup(name);

    SymbolTableIterator iter = st.find(name);
    if (iter != st.end())
        return (*iter).second;
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

std::string Value::asString() const {
    std::ostringstream ss;
    ss << *this ;
	std::string s(ss.str());
    return s;
}

bool Value::asInteger(long &x) const {
	if (kind == t_integer) {
		x = iValue;
		return true;
	}
	if (kind == t_string) {
		char *p;
		x = strtol(sValue.c_str(), &p, 0);
		if (*p == 0) return true;
		return false;
	}
	else if (kind == t_bool) {
		x = (bValue) ? 1 : 0;
		return true;
	}
	return false;
}

std::ostream &SymbolTable::operator <<(std::ostream & out) const {
    SymbolTableConstIterator iter = st.begin();
    const char *delim = "";
    while (iter != st.end()) {
        out << delim << (*iter).first << ':' << (*iter).second;
        delim = ",";
        iter++;
    }
    return out;
}

std::ostream &operator <<( std::ostream &out, const SymbolTable &st) {
    return st.operator<<(out);
}

