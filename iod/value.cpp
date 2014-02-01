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
#include "dynamic_value.h"

void Value::setDynamicValue(DynamicValue *dv) {
    if (kind == t_dynamic) { dyn_value = dyn_value->deref(); }
    kind = t_dynamic;
    dyn_value = DynamicValue::ref(dv);
}

void Value::setDynamicValue(DynamicValue &dv) {
    if (kind == t_dynamic) { dyn_value = dyn_value->deref(); }
    kind = t_dynamic;
    dyn_value = DynamicValue::ref(&dv);
}

Value::~Value() {
    if (kind == t_dynamic) { dyn_value = dyn_value->deref(); }
}

Value::Value(const Value&other) :kind(other.kind), bValue(other.bValue), iValue(other.iValue),
    sValue(other.sValue), cached_machine(other.cached_machine), dyn_value(DynamicValue::ref(other.dyn_value)) {
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

Value::Value(DynamicValue &dv) : kind(t_dynamic) {
    dyn_value = DynamicValue::ref(&dv);
}

// this form takes ownership of the passed DynamiValue rather than makes a clone
Value::Value(DynamicValue *dv) : kind(t_dynamic) {
    dyn_value = DynamicValue::ref(dv);
}

Value Value::operator=(const Value &orig){
//    listValue.erase(listValue.begin(), listValue.end());
    kind=orig.kind;
    if (dyn_value) { dyn_value = dyn_value->deref(); }
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
        case t_dynamic:
            dyn_value = DynamicValue::ref(orig.dyn_value);
            kind=orig.kind;
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
        case t_empty: return b == t_empty;
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
        case t_empty: return b != t_empty;
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
        case t_string: out <<'"'<< sValue << '"'; break;
#if 0
        case t_list:   { 
            std::ostream_iterator<Value> o_iter(out, ","); 
            std::copy(listValue.begin(), listValue.end(), o_iter);
            out << "(" << listValue.size() << " values)"; 
        }
            breawk;
        case t_map: {
            if (mapValue.size())
                out << "(Properties)";
        }
            break;
#endif
        case t_bool: {
            out << ((bValue) ? "true" : "false");
        }
            break;
		case t_dynamic:
            if (dyn_value) dyn_value->operator<<(out); else out << "<null>";
            break;
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const Value &val){ return val.operator<<(out); }

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

std::string Value::asString() const {
    if (kind == t_string) return this->sValue;
    std::ostringstream ss;
    ss << *this;
	std::string s(ss.str());
    return s;
}

std::string Value::quoted() const {
    std::string res = "\"";
    res += this->asString() + "\"";
    return res;
}

bool Value::asInteger(long &x) const {
	if (kind == t_integer) {
		x = iValue;
		return true;
	}
	if (kind == t_string || kind == t_symbol) {
		char *p;
		const char *v = sValue.c_str();
		x = strtol(v, &p, 0);
		if (*p == 0 || p != v ) return true;
		return false;
	}
	else if (kind == t_bool) {
		x = (bValue) ? 1 : 0;
		return true;
	}
	return false;
}
