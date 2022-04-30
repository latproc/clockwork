/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc

    Latproc is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    Latproc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Latproc; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA   02110-1301, USA.
*/

#include "DebugExtra.h"
#include "Logger.h"
#include "MessageLog.h"
#include "clock.h"
#include "symboltable.h"
#include <boost/foreach.hpp>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits.h>
#include <math.h>
#include <numeric>
#include <sstream>
#include <stdio.h>
#include <utility>

static bool stringToLong(const std::string &s, long &x);
static bool stringToFloat(const std::string &s, double &x);

uint64_t microsecs() { return Clock::clock(); }

static const double ZERO_DISTANCE = 1.0E-8;

void simple_deltat(std::ostream &out, uint64_t dt) {
    if (dt > 60000000) {
        out << std::setprecision(3) << (float)dt / 60000000.0f << "m";
    }
    else if (dt > 1000000) {
        out << std::setprecision(3) << (float)dt / 1000000.0f << "s";
    }
    else if (dt > 1000) {
        out << std::setprecision(3) << (float)dt / 1000.0f << "ms";
    }
    else {
        out << dt << "us";
    }
}

DynamicValueBase::~DynamicValueBase() {}

Value::Value() : kind(t_empty), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {}

Value::Value(Kind k) : kind(k), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {}

Value::Value(bool v)
    : kind(t_bool), bValue(v), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {}

Value::Value(long v)
    : kind(t_integer), iValue(v), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {}

Value::Value(int v)
    : kind(t_integer), iValue(v), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {}

Value::Value(unsigned int v)
    : kind(t_integer), iValue(v), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {}

Value::Value(unsigned long v)
    : kind(t_integer), iValue(v), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {}

Value::Value(float v)
    : kind(t_float), fValue(v), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {}

Value::Value(double v)
    : kind(t_float), fValue(v), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {}

Value::Value(const char *str, Kind k)
    : kind(k), sValue(str), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {
    if (kind == t_symbol && !sValue.empty()) {
        if (sValue == "FALSE") {
            sValue = "";
            kind = t_bool;
            bValue = false;
        }
        else if (sValue == "TRUE") {
            sValue = "";
            kind = t_bool;
            bValue = true;
        }
        else if (sValue == "NULL") {
            kind = t_empty;
            sValue = "";
        }
        else {
            token_id = Tokeniser::instance()->getTokenId(str);
        }
    }
}

Value::Value(std::string str, Kind k)
    : kind(k), sValue(str), cached_machine(0), cached_value(0), token_id(0), dyn_value(0) {
    if (kind == t_symbol && !str.empty()) {
        if (sValue == "FALSE") {
            sValue = "";
            kind = t_bool;
            bValue = false;
        }
        else if (sValue == "TRUE") {
            sValue = "";
            kind = t_bool;
            bValue = true;
        }
        else if (sValue == "NULL") {
            kind = t_empty;
            sValue = "";
        }
        token_id = Tokeniser::instance()->getTokenId(str);
    }
}

bool Value::isNull() const { return kind == t_empty; }

void Value::setDynamicValue(DynamicValueBase *dv) {
    if (kind == t_dynamic && dyn_value) {
        dyn_value = dyn_value->deref();
    }
    else {
        assert(dyn_value == nullptr);
    }
    kind = t_dynamic;
    dyn_value = DynamicValueBase::ref(dv);
}

void Value::setDynamicValue(DynamicValueBase &dv) {
    if (kind == t_dynamic) {
        dyn_value = dyn_value->deref();
    }
    else {
        assert(dyn_value == nullptr);
    }
    kind = t_dynamic;
    dyn_value = DynamicValueBase::ref(&dv);
}

Value::~Value() {
    if (kind == t_dynamic) {
        if (dyn_value) {
            dyn_value = dyn_value->deref();
        }
    }
    else {
        assert(dyn_value == nullptr);
    }
}

Value::Value(const Value &other)
    : kind(other.kind), bValue(other.bValue), iValue(other.iValue), fValue(other.fValue),
      sValue(other.sValue), cached_machine(other.cached_machine), cached_value(0),
      token_id(other.token_id), dyn_value(DynamicValueBase::ref(other.dyn_value)) {
    //      if (kind == t_list) {
    //              std::copy(other.listValue.begin(), other.listValue.end(), std::back_inserter(listValue));
    //      }
    //      else if (kind == t_map) {
    //              std::pair<std::string, Value> node;
    //              BOOST_FOREACH(node, other.mapValue) {
    //                      mapValue[node.first] = node.second;
    //              }
    //      }
}

Value::Value(DynamicValueBase &dv) : kind(t_dynamic), cached_value(0) {
    dyn_value = DynamicValueBase::ref(&dv);
}

// this form takes ownership of the passed DynamiValue rather than makes a clone
Value::Value(DynamicValueBase *dv) : kind(t_dynamic), cached_value(0) {
    dyn_value = DynamicValueBase::ref(dv);
}

void Value::toString() {
    if (kind == t_symbol) {
        kind = t_string;
    }
}

void Value::toSymbol() {
    if (kind == t_string) {
        kind = t_symbol;
    }
}

Value &Value::operator=(const Value &orig) {
    //      listValue.erase(listValue.begin(), listValue.end());
    kind = orig.kind;
    if (dyn_value) {
        dyn_value = dyn_value->deref();
    }
    switch (kind) {
    case t_bool:
        bValue = orig.bValue;
        break;
    case t_integer:
        iValue = orig.iValue;
        break;
    case t_float:
        fValue = orig.fValue;
        break;
    case t_string:
        sValue = orig.sValue;
        break;
    case t_symbol:
        sValue = orig.sValue;
        token_id = orig.token_id;
        break;
    case t_dynamic:
        dyn_value = DynamicValueBase::ref(orig.dyn_value);
        kind = orig.kind;
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
    cached_value = orig.cached_value;
    return *this;
}

Value &Value::operator=(bool val) {
    *this = Value(val);
    return *this;
}

Value &Value::operator=(int val) {
    *this = Value(val);
    return *this;
}

Value &Value::operator=(long val) {
    *this = Value(val);
    return *this;
}

Value &Value::operator=(unsigned long val) {
    *this = Value(val);
    return *this;
}

Value &Value::operator=(float val) {
    *this = Value(val);
    return *this;
}

Value &Value::operator=(double val) {
    *this = Value(val);
    return *this;
}

Value &Value::operator=(const char *val) {
    *this = Value(val);
    return *this;
}

Value &Value::operator=(std::string val) {
    *this = Value(val);
    return *this;
}

long Value::trunc() const {
    if (kind == t_integer) {
        return iValue;
    }
    if (kind == t_float) {
        return (long)::trunc(fValue);
    }
    return 0;
};

long Value::round(int digits) const {
    if (kind == t_integer) {
        return iValue;
    }
    if (kind == t_float) {
        return (long)::round(fValue);
    }
    return 0;
}

double Value::toFloat() const {
    if (kind == t_integer) {
        return iValue;
    }
    if (kind == t_float) {
        return fValue;
    }
    double x;
    if (kind == t_string) {
        std::cerr << "Warning: converting a string to float\n";
        if (stringToFloat(sValue, x)) {
            return x;
        }
    }
    else if (kind == t_symbol) {
        std::cerr << "Warning: converting a string to float when passed a symbol\n";
    }
    //TBD assert(false);
    return 0.0;
}

bool Value::numeric() const { return kind == t_integer || kind == t_float; }

bool Value::identical(const Value &other) const {
    if (kind != t_float || other.kind != t_float) {
        return *this == other;
    }
    else {
        return fValue == other.fValue;
    }
}

std::string Value::name() const {
    switch (kind) {
    case t_symbol:
    case t_string:
        return sValue;
        break;
#if 0
        case t_map:
        case t_list: {
            Value v = *(listValue.begin());
            return v.name();
        }
#endif
    default:;
    }
    return "Untitled";
}

#if 0
Value Value::operator[](int index)
{
    if (kind != t_list)
        if (index == 0) {
            return *this;
        }
        else {
            return 0;
        }
    if (index < 0 || index >= listValue.size()) {
        return 0;
    }
    int i = 0;
    List::iterator iter = listValue.begin();
    while (iter != listValue.end() && i++ < index) {
        iter++;
    }
    return *iter;
}
#endif

bool numeric_types(Value::Kind a, Value::Kind b) {
    return (a == Value::t_integer || a == Value::t_float) &&
           (b == Value::t_integer || b == Value::t_float);
}

bool Value::operator>=(const Value &other) const {
    Kind a = kind;
    Kind b = other.kind;

    if (a == t_symbol) {
        a = t_string;
    }
    if (b == t_symbol) {
        b = t_string;
    }

    if (numeric_types(a, b) || (a != b && (a == t_string || b == t_string))) {

        if (a == t_float || b == t_float) {
            double x, y;
            if (asFloat(x) && other.asFloat(y)) {
                return x >= y;
            }
            else {
                return false;
            }
        }

        if (a == t_integer || b == t_integer) {
            long x, y;
            if (asInteger(x) && other.asInteger(y)) {
                return x >= y;
            }
            else {
                return false;
            }
        }
    }

    if (a != b) {
        return false;
    }
    switch (kind) {
    case t_empty:
        return false;
        break;
    case t_integer:
        return iValue >= other.iValue;
        break;
    case t_float:
        return fValue >= other.fValue;
        break;
    case t_symbol:
    case t_string:
        return sValue >= other.sValue;
        break;
    default:
        break;
    }
    return false;
}

bool Value::operator<=(const Value &other) const {
    Kind a = kind;
    Kind b = other.kind;

    if (a == t_symbol) {
        a = t_string;
    }
    if (b == t_symbol) {
        b = t_string;
    }

    if (numeric_types(a, b) || (a != b && (a == t_string || b == t_string))) {
        if (a == t_float || b == t_float) {
            double x, y;
            if (asFloat(x) && other.asFloat(y)) {
                return x <= y;
            }
            else {
                return false;
            }
        }

        if (a == t_integer || b == t_integer) {
            long x, y;
            if (asInteger(x) && other.asInteger(y)) {
                return x <= y;
            }
            else {
                return false;
            }
        }
    }

    if (a != b) {
        return false;
    }
    switch (kind) {
    case t_empty:
        return false;
        break;
    case t_integer:
        return iValue <= other.iValue;
        break;
    case t_float:
        return fValue <= other.fValue;
        break;
    case t_symbol:
    case t_string:
        return sValue <= other.sValue;
        break;
    default:
        break;
    }
    return false;
}

bool Value::operator==(const Value &other) const {

    Kind a = kind;
    Kind b = other.kind;

    if (a == t_symbol && b == t_symbol) {
        return token_id == other.token_id;
    }

    if (a == t_symbol) {
        a = t_string;
    }
    if (b == t_symbol) {
        b = t_string;
    }

    if (numeric_types(a, b) || (a != b && (a == t_string || b == t_string))) {
        if (a == t_float || b == t_float) {
            double x, y;
            if (asFloat(x) && other.asFloat(y)) {
                return x == y || fabs(x - y) <= ZERO_DISTANCE;
            }
            else {
                return false;
            }
        }

        if (a == t_integer || b == t_integer) {
            long x, y;
            if (asInteger(x) && other.asInteger(y)) {
                return x == y;
            }
            else {
                return false;
            }
        }
    }

    if (a != b) {
        return false; // different types cannot be equal (yet)
    }
    switch (a) {
    case t_empty:
        return b == t_empty;
        break;
    case t_float:
        return fValue == other.fValue || fabs(fValue - other.fValue) <= ZERO_DISTANCE;
        break;
    case t_integer:
        return iValue == other.iValue;
        break;
    case t_symbol: //TBD assert(false);
        return token_id == other.token_id;
    case t_string:
        return sValue == other.sValue;
        break;
    case t_bool:
        return bValue == other.bValue;
        break;
    default:
        break;
    }
    return false;
}

bool Value::operator!=(const Value &other) const {
    Kind a = kind;
    Kind b = other.kind;

    if (a == t_symbol && b == t_symbol) {
        return token_id != other.token_id;
    }

    if (a == t_symbol) {
        a = t_string;
    }
    if (b == t_symbol) {
        b = t_string;
    }

    if (numeric_types(a, b) || (a != b && (a == t_string || b == t_string))) {
        if (a == t_float || b == t_float) {
            double x, y;
            if (asFloat(x) && other.asFloat(y)) {
                return x != y && fabs(x - y) > ZERO_DISTANCE;
            }
            else {
                return true;
            }
        }
        if (a == t_integer || b == t_integer) {
            long x, y;
            if (asInteger(x) && other.asInteger(y)) {
                return x != y;
            }
            else {
                return true;
            }
        }
    }

    if (a != b) {
        return true;
    }
    switch (a) {
    case t_empty:
        return b != t_empty;
        break;
    case t_integer:
        return iValue != other.iValue;
        break;
    case t_float:
        return fValue != other.fValue && fabs(fValue - other.fValue) > ZERO_DISTANCE;
        break;
    case t_symbol:
        //TBD assert(false);
        return token_id != other.token_id;
    case t_string:
        return sValue != other.sValue;
        break;
    case t_bool:
        return bValue != other.bValue;
        break;
    default:
        break;
    }
    return false;
}

bool Value::operator&&(const Value &other) const {
    switch (kind) {
    case t_empty:
        return false;
        break;
    case t_integer:
        return iValue && other.iValue;
        break;
    case t_bool:
        return bValue && other.bValue;
        break;
    default:
        break;
    }
    return false;
}

bool Value::operator||(const Value &other) const {
    switch (kind) {
    case t_empty:
        return false;
        break;
    case t_integer:
        return iValue || other.iValue;
        break;
    case t_bool:
        return bValue || other.bValue;
        break;
    default:
        break;
    }
    return false;
}

bool Value::operator!() const {
    switch (kind) {
    case t_empty:
        return true;
        break;
    case t_integer:
        return !iValue;
        break;
    case t_bool:
        return !bValue;
        break;
    case t_symbol:
    case t_string:
        return false;
        break;
    default:
        break;
    }
    return true;
}

static bool stringToLong(const std::string &s, long &x) {
    const char *str = s.c_str();
    char *end;
    x = strtol(str, &end, 10);
    if (*end) {
        // check if this is actually a float before writing an error message
        if (*end == '.') {
            char *p = end;
            while (isdigit(*(++p))) {
                ;
            }
            if (*p == 0) {
                return false;
            }
        }

        char buf[200];
        snprintf(buf, 200, "str to long parsed %ld chars but did not hit the end of string '%s'",
                 end - str, str);
        MessageLog::instance()->add(buf);
        DBG_PREDICATES << buf << "'\n";
    }
    return *end == 0;
}

static bool stringToFloat(const std::string &s, double &x) {
    const char *str = s.c_str();
    char *end;
    x = strtod(str, &end);
    if (*end) {
        char buf[200];
        snprintf(buf, 200, "str to float parsed %ld chars but did not hit the end of string '%s'",
                 end - str, str);
        MessageLog::instance()->add(buf);
        DBG_PREDICATES << buf << "'\n";
    }
    return *end == 0;
}

namespace ValueOperations {
struct ValueOperation {
    virtual Value operator()(const Value &a, const Value &b) const { return 0; }
    virtual std::ostream &operator<<(std::ostream &out) const { return out; }
    virtual std::string toString() const { return ""; }
};
std::ostream &operator<<(std::ostream &out, const ValueOperation &v) { return v.operator<<(out); }

struct Sum : public ValueOperation {
    Value operator()(const Value &a, const Value &b) const {
        if (a.kind == Value::t_integer) {
            if (b.kind == Value::t_integer) {
                return a.iValue + b.iValue;
            }
            else if (b.kind == Value::t_float) {
                return b.fValue + (double)a.iValue;
            }
            else {
                return a.iValue;
            }
        }
        else if (a.kind == Value::t_float) {
            if (b.kind == Value::t_float) {
                return a.fValue + b.fValue;
            }
            else if (b.kind == Value::t_integer) {
                return a.fValue + (double)b.iValue;
            }
            else {
                return a.fValue;
            }
        }
        else if (b.kind == Value::t_integer) {
            return b.iValue;
        }
        else if (b.kind == Value::t_float) {
            return b.fValue;
        }
        else if (a.kind == Value::t_string) {
            return Value(a.sValue + b.asString(), Value::t_string);
        }
        else if (a.kind == Value::t_symbol) {
            return Value(a.sValue + b.asString(), a.kind);
        }
        return 0;
    }
    std::ostream &operator<<(std::ostream &out) const {
        out << "add";
        return out;
    }
    virtual std::string toString() const { return "add"; }
};

struct Minus : public ValueOperation {
    Value operator()(const Value &a, const Value &b) const {
        if (a.kind == Value::t_integer) {
            if (b.kind == Value::t_integer) {
                return a.iValue - b.iValue;
            }
            else if (b.kind == Value::t_float) {
                return (double)a.iValue - b.fValue;
            }
            else {
                return a.iValue;
            }
        }
        else if (a.kind == Value::t_float) {
            if (b.kind == Value::t_float) {
                return a.fValue - b.fValue;
            }
            else if (b.kind == Value::t_integer) {
                return a.fValue - (double)b.iValue;
            }
            else {
                return a.fValue;
            }
        }
        else if (b.kind == Value::t_integer) {
            return -b.iValue;
        }
        else if (b.kind == Value::t_float) {
            return -b.fValue;
        }
        else if (a.kind == Value::t_string) {
            return a;
        }
        else if (a.kind == Value::t_symbol) {
            return a;
        }
        return 0;
    }
    std::ostream &operator<<(std::ostream &out) const {
        out << "subtract";
        return out;
    }
    virtual std::string toString() const { return "subtract"; }
};

struct Multiply : public ValueOperation {
    Value operator()(const Value &a, const Value &b) const {
        if (a.kind == Value::t_integer) {
            if (b.kind == Value::t_integer) {
                return a.iValue * b.iValue;
            }
            else if (b.kind == Value::t_float) {
                return b.fValue * (double)a.iValue;
            }
            else {
                return 0;
            }
        }
        else if (a.kind == Value::t_float) {
            if (b.kind == Value::t_float) {
                return a.fValue * b.fValue;
            }
            else if (b.kind == Value::t_integer) {
                return a.fValue * (double)b.iValue;
            }
            else {
                return 0;
            }
        }
        return 0;
    }
    std::ostream &operator<<(std::ostream &out) const {
        out << "multiply";
        return out;
    }
    virtual std::string toString() const { return "multiply"; }
};

struct Divide : public ValueOperation {
    Value operator()(const Value &a, const Value &b) const {
        if (a.kind == Value::t_integer) {
            if (a.iValue == 0) {
                return a;
            }
            if (b.kind == Value::t_integer) {
                if (b.iValue == 0) {
                    if (a.iValue < 0) {
                        return INT_MIN;
                    }
                    else {
                        return INT_MAX;
                    }
                }
                else {
                    return a.iValue / b.iValue;
                }
            }
            else if (b.kind == Value::t_float)
                if (b.fValue == 0) {
                    if (a.iValue < 0) {
                        return INT_MIN;
                    }
                    else {
                        return INT_MAX;
                    }
                }
                else {
                    return (double)a.iValue / b.fValue;
                }
            else {
                return a;
            }
        }
        else if (a.kind == Value::t_float) {
            if (a.fValue == 0) {
                return 0.0;
            }
            if (b.kind == Value::t_float) {
                if (b.fValue == 0.0) {
                    if (a.fValue < 0) {
                        return INT_MIN;
                    }
                    else {
                        return INT_MAX;
                    }
                }
                else {
                    return a.fValue / b.fValue;
                }
            }
            else if (b.kind == Value::t_integer)
                if (b.iValue == 0) {
                    if (a.fValue < 0) {
                        return INT_MIN;
                    }
                    else {
                        return INT_MAX;
                    }
                }
                else {
                    return a.fValue / b.iValue;
                }
            else {
                return 0;
            }
        }
        return a;
    }
    std::ostream &operator<<(std::ostream &out) const {
        out << "divide";
        return out;
    }
    virtual std::string toString() const { return "divide"; }
};

struct Modulus : public ValueOperation {
    Value operator()(const Value &a, const Value &b) const {
        if (b.kind != Value::t_integer) {
            return a;
        }
        if (b.iValue == 0) {
            return 0;
        }
        else {
            if (a.kind == Value::t_integer) {
                return a.iValue % b.iValue;
            }
            else if (a.kind == Value::t_float) {
                return (long)trunc(a.fValue) % b.iValue;
            }
            else {
                return 0;
            }
        }
    }
    std::ostream &operator<<(std::ostream &out) const {
        out << "modulus";
        return out;
    }
    virtual std::string toString() const { return "modulus"; }
};

struct BitAnd : public ValueOperation {
    Value operator()(const Value &a, const Value &b) const { return a.iValue & b.iValue; }
    std::ostream &operator<<(std::ostream &out) const {
        out << "AND";
        return out;
    }
    virtual std::string toString() const { return "AND"; }
};

struct BitOr : public ValueOperation {
    Value operator()(const Value &a, const Value &b) const { return a.iValue | b.iValue; }
    std::ostream &operator<<(std::ostream &out) const {
        out << "OR";
        return out;
    }
    virtual std::string toString() const { return "OR"; }
};

struct BitXOr : public ValueOperation {
    Value operator()(const Value &a, const Value &b) const { return a.iValue ^ b.iValue; }
    std::ostream &operator<<(std::ostream &out) const {
        out << "XOR";
        return out;
    }
    virtual std::string toString() const { return "XOR"; }
};

} // namespace ValueOperations
using namespace ValueOperations;

struct TypeFix {
    Value operator()(const Value &a, ValueOperation *op, const Value &b) {
        if (a.kind != b.kind) {
            long x;
            double x_float;
            if ((a.kind == Value::t_integer || a.kind == Value::t_float) &&
                (b.kind == Value::t_string || b.kind == Value::t_symbol)) {
                if (stringToLong(b.sValue, x)) {
                    v_ = x;
                    return (*op)(a, v_);
                }
                else if (stringToFloat(b.sValue, x_float)) {
                    v_ = x_float;
                    return (*op)(a, v_);
                }
                else {
                    char buf[200];
                    snprintf(buf, 200,
                             "Trying to %s %s and %s but the string does not contain a number",
                             op->toString().c_str(), a.asString().c_str(), b.asString().c_str());
                    MessageLog::instance()->add(buf);
                    DBG_PREDICATES << buf << "\n";
                    return a;
                }
            }
            else if (a.kind == Value::t_string || a.kind == Value::t_symbol) {
                return (*op)(a, Value(b.asString(), Value::t_string));
            }
            /*
                else if (b.kind == Value::t_integer && (a.kind == Value::t_string || a.kind == Value::t_symbol) ){
                if (stringToLong(a.sValue, x)) {
                    v_ = x;
                    return (*op)(v_, b);
                }
                else {
                    //DBG_PREDICATES << "Trying to add a string and value but the string does not contain a number\n";
                                        v_ = a;
                    v_ = v_.operator+(b.asString());
                }
                }
            */
            else if ((a.kind == Value::t_integer && b.kind == Value::t_float) ||
                     (a.kind == Value::t_float && b.kind == Value::t_integer)) {
                return (*op)(a, b);
            }
            else {
                DBG_PREDICATES << " type clash, returning lhs a is" << a.kind << " and b is "
                               << b.kind << "\n";
                return a;
            }
        }
        DBG_PREDICATES << "invalid call to TypeFix operator when type are the same for: " << a
                       << " and " << b << "\n";
        // TBD assert(false);
        return a; // invalid usage
    }
    Value &value() { return v_; }

  private:
    Value v_;
};

Value Value::operator+(const Value &other) {
    Value res(*this);
    return res += other;
}

Value &Value::operator+=(const Value &other) {
    if (kind != other.kind) {
        Sum op;
        TypeFix tf;
        Value v(tf(*this, &op, other));
        return operator=(v);
    }
    switch (kind) {
    case t_integer:
        iValue += other.iValue;
        break;
    case t_float:
        fValue += other.fValue;
        break;
    case t_bool:
        bValue |= other.bValue;
    case t_symbol:
    case t_string:
        sValue += other.asString();
        token_id = 0;
        break;
    default:;
    }
    return *this;
}

Value Value::operator-(void) const {
    switch (kind) {
    case t_integer:
        return Value(-iValue);
    case t_float:
        return Value(-fValue);
        break;
    case t_symbol:
    case t_string:
        long x;
        if (stringToLong(sValue, x)) {
            return -x;
        }
        char *end;
        x = strtol(sValue.c_str(), &end, 10);
        if (*end == 0) {
            return Value(-x);
        }
    default:;
    }
    return *this;
}

Value Value::operator-(const Value &other) {
    Value res(*this);
    return res -= other;
}

Value &Value::operator-=(const Value &other) {
    if (kind != other.kind) {
        Minus op;
        TypeFix tf;
        return operator=(tf(*this, &op, other));
    }
    switch (kind) {
    case t_integer:
        iValue -= other.iValue;
        break;
    case t_float:
        fValue -= other.fValue;
        break;
    case t_bool:
        bValue |= other.bValue;
    default:;
    }
    return *this;
}

Value Value::operator*(const Value &other) {
    Value res(*this);
    return res *= other;
}

Value &Value::operator*=(const Value &other) {
    if (kind != other.kind) {
        if (kind == t_string || kind == t_symbol) {
            char buf[200];
            snprintf(buf, 200, "Warning: multiplying %s(string) by %s%s", this->asString().c_str(),
                     other.asString().c_str(),
                     (other.kind == t_string || other.kind == t_symbol) ? "(string)" : "");
            MessageLog::instance()->add(buf);
            NB_MSG << buf << "\n";
            return *this;
        }
        Multiply op;
        TypeFix tf;
        return operator=(tf(*this, &op, other));
    }
    switch (kind) {
    case t_integer:
        iValue *= other.iValue;
        break;
    case t_float:
        fValue *= other.fValue;
        break;
    case t_bool:
        bValue &= other.bValue;
    default:;
    }
    return *this;
}

Value Value::operator/(const Value &other) {
    Value res(*this);
    return res /= other;
}

Value &Value::operator/=(const Value &other) {
    if (kind != other.kind) {
        Divide op;
        TypeFix tf;
        return operator=(tf(*this, &op, other));
    }
    switch (kind) {
    case t_integer:
        if (iValue != 0) {
            if (other.iValue == 0) {
                if (iValue < 0) {
                    iValue = INT_MIN;
                }
                else {
                    iValue = INT_MAX;
                }
            }
            else {
                iValue /= other.iValue;
            }
        }
        break;
    case t_float:
        if (fValue != 0) {
            if (other.fValue == 0) {
                if (fValue < 0) {
                    fValue = INT_MIN;
                }
                else {
                    fValue = INT_MAX;
                }
            }
            else {
                fValue /= other.fValue;
            }
        }
        break;
    case t_bool:
        bValue &= other.bValue;
    default:;
    }
    return *this;
}
Value Value::operator%(const Value &other) {
    Value res(*this);
    return res %= other;
}

Value &Value::operator%=(const Value &other) {
    if (kind != other.kind) {
        Modulus op;
        TypeFix tf;
        return operator=(tf(*this, &op, other));
    }
    switch (kind) {
    case t_integer:
        if (other.iValue == 0) {
            iValue = 0;
        }
        else {
            iValue = iValue % other.iValue;
        }
        break;
    case t_float:
        if (other.fValue == 0.0) {
            iValue = 0;
        }
        else {
            iValue = ((long)fValue) % other.iValue;
        }
        kind = t_integer; // modulus returns an integer result
        break;
    case t_bool:
        bValue ^= other.bValue;
    default:;
    }
    return *this;
}

Value Value::operator&(const Value &other) {
    Value res(*this);
    return res &= other;
}

Value &Value::operator&=(const Value &other) {
    if (kind != other.kind) {
        BitAnd op;
        TypeFix tf;
        return operator=(tf(*this, &op, other));
    }
    switch (kind) {
    case t_integer:
        iValue = iValue & other.iValue;
        break;
    case t_bool:
        bValue &= other.bValue;
    default:;
    }
    return *this;
}

Value Value::operator|(const Value &other) {
    Value res(*this);
    return res |= other;
}

Value &Value::operator|=(const Value &other) {
    if (kind != other.kind) {
        BitOr op;
        TypeFix tf;
        return operator=(tf(*this, &op, other));
    }
    switch (kind) {
    case t_integer:
        iValue = iValue | other.iValue;
        break;
    case t_bool:
        bValue |= other.bValue;
    default:;
    }
    return *this;
}

Value Value::operator^(const Value &other) {
    Value res(*this);
    return res ^= other;
}

Value &Value::operator^=(const Value &other) {
    if (kind != other.kind) {
        BitXOr op;
        TypeFix tf;
        return operator=(tf(*this, &op, other));
    }
    switch (kind) {
    case t_integer:
        iValue = iValue ^ other.iValue;
        break;
    case t_bool:
        bValue ^= other.bValue;
    default:;
    }
    return *this;
}

Value &Value::operator~() {
    switch (kind) {
    case t_integer:
        iValue = ~iValue;
        break;
    case t_bool:
        bValue = !bValue;
    default:;
    }
    return *this;
}

#if 0
Value Value::operator[](std::string key)
{
    if (kind != t_map) {
        return 0;
    }
    Map::iterator iter = mapValue.find(key);
    if (iter == mapValue.end()) {
        return 0;
    }
    return (*iter).second;
}
#endif

std::ostream &Value::operator<<(std::ostream &out) const {
    switch (kind) {
    case t_empty:
        out << "(empty)";
        break;
    case t_integer:
        out << iValue;
        break;
    case t_float: {
        if (fValue != 0.0 && fValue <= 1.0e-4 && fValue >= -1.0e-4) {
            out << std::scientific << fValue;
        }
        else {
            int prec = (fValue == 0.0) ? 1 : 6;
            out << std::setprecision(prec) << std::fixed << fValue;
        }
        break;
    }
    case t_symbol:
        out << sValue;
        break;
    case t_string:
        out << '"' << sValue << '"';
        break;
#if 0
        case t_list:     {
            std::ostream_iterator<Value> o_iter(out, ",");
            std::copy(listValue.begin(), listValue.end(), o_iter);
            out << "(" << listValue.size() << " values)";
        }
        break;
        case t_map: {
            if (mapValue.size()) {
                out << "(Properties)";
            }
        }
        break;
#endif
    case t_bool: {
        out << ((bValue) ? "true" : "false");
    } break;
    case t_dynamic:
        if (dyn_value) {
            dyn_value->operator<<(out);
        }
        else {
            out << "<null>";
        }
        break;
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const Value &val) { return val.operator<<(out); }

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

std::string Value::asString(const char *fmt) const {
    switch (kind) {
    case t_bool:
        return (bValue) ? "true" : "false";
        break;
    case t_integer: {
        char buf[25];
        if (fmt) {
            snprintf(buf, 25, fmt, iValue);
        }
        else {
            snprintf(buf, 25, "%ld", iValue);
        }
        return buf;
    }
    case t_float: {
        char buf[25];
        if (fmt) {
            snprintf(buf, 25, fmt, fValue);
        }
        else {
            snprintf(buf, 25, "%6.6lf", fValue);
        }
        return buf;
    }
    case t_empty:
        return "null";
    case t_symbol:
    case t_string:
        return sValue;
    case t_dynamic: {
        const Value &tmp((*dyn_value)());
        return tmp.asString();
    }

    default:
        break;
    }
    return "";
}

std::string Value::quoted() const {
    std::string val = this->asString();
    if (val[0] != '\"' || val.back() != '\"') {
        std::string res = "\"";
        res += val + "\"";
        return res;
    }
    else {
        return val;
    }
}

bool Value::asBoolean(bool &x) const {
    switch (kind) {
    case t_bool: {
        x = bValue;
        return true;
    }
    case t_integer: {
        x = iValue != 0;
        return true;
    }
    case t_float: {
        x = fValue != 0.0;
        return true;
    }
    case t_string:
    case t_symbol:
        if (sValue == "true" || sValue == "TRUE") {
            x = true;
            return true;
        }
        else if (sValue == "false" || sValue == "FALSE") {
            x = false;
            return true;
        }
        else {
            return false;
        }
    case t_dynamic: {
        const Value &tmp((*dyn_value)());
        return tmp.asBoolean(x);
    }
    default:
        return false;
    }
}

bool Value::asInteger(long &x) const {
    if (kind == t_integer) {
        x = iValue;
        return true;
    }
    if (kind == t_float) {
        x = fValue;
        return true;
    }
    if (kind == t_string || kind == t_symbol) {
        char *p;
        const char *v = sValue.c_str();
        x = strtol(v, &p, 10);
        if (*p == 0) {
            return true;
        }
        if (p != v) {
            return false;
        }
        return false;
    }
    else if (kind == t_bool) {
        x = (bValue) ? 1 : 0;
        return true;
    }
    else if (kind == t_dynamic) {
        const Value &tmp((*dyn_value)());
        return tmp.asInteger(x);
    }
    return false;
}

bool Value::asFloat(double &x) const {
    if (kind == t_float) {
        x = fValue;
        return true;
    }
    if (kind == t_integer) {
        x = (double)iValue;
        return true;
    }
    if (kind == t_string || kind == t_symbol) {
        char *p;
        const char *v = sValue.c_str();
        x = strtod(v, &p);
        if (*p == 0) {
            return true;
        }
        if (p != v) {
            return false;
        }
        return false;
    }
    else if (kind == t_bool) {
        x = (bValue) ? 1.0 : 0.0;
        return true;
    }
    else if (kind == t_dynamic) {
        const Value &tmp((*dyn_value)());
        return tmp.asFloat(x);
    }
    return false;
}
