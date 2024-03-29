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

#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <string>

class MachineInstance;
class DynamicValue;
class Value;

uint64_t microsecs();
void simple_deltat(std::ostream &out, uint64_t dt);

class DynamicValueBase {
  public:
    virtual ~DynamicValueBase();
    static DynamicValueBase *ref(DynamicValueBase *dv) {
        if (!dv) {
            return nullptr;
        }
        else {
            dv->refs++;
        }
        return dv;
    }
    DynamicValueBase *deref() {
        --refs;
        if (!refs) {
            delete this;
        }
        return nullptr;
    }
    virtual DynamicValueBase *clone() const = 0;
    virtual const Value *lastResult() const = 0;
    virtual void setScope(MachineInstance *m) = 0;
    virtual MachineInstance *getScope() const = 0;

    virtual std::ostream &operator<<(std::ostream &) const = 0;
    virtual const Value &
    operator()(MachineInstance *scope) = 0; // uses the provided machine's scope
    virtual const Value &operator()() = 0;  // uses the current scope for evaluation
  protected:
    int refs = 0;
};
std::ostream &operator<<(std::ostream &, const DynamicValue &);

class Value {
  public:
    enum Kind {
        t_empty,
        t_integer,
        t_string,
        t_bool,
        t_symbol,
        t_dynamic,
        t_float /*, t_list, t_map */
    };

    typedef std::list<Value *> List;
    //    typedef std::map<std::string, Value> Map;

    Value();
    Value(Kind k);
    Value(bool v);
    Value(long v);
    Value(int v);
    Value(unsigned int v);
    Value(unsigned long v);
    Value(float v);
    Value(double v);
    Value(const char *str, Kind k = t_symbol);
    Value(std::string str, Kind k = t_symbol);
    Value(const Value &other);
    Value(DynamicValueBase *dv);
    Value(DynamicValueBase &dv);
    virtual ~Value();
    std::string asString(const char *format = nullptr) const;
    std::string quoted() const;
    bool isNull() const;
    bool asFloat(double &val) const;
    bool asInteger(long &val) const;
    bool asBoolean(bool &val) const;
    long trunc() const;
    long round(int digits = 0) const;
    double toFloat() const;
    explicit operator long() const { return iValue; }
    explicit operator int() const { return (int)iValue; }
    explicit operator float() const { return (float)fValue; }
    explicit operator double() const { return fValue; }
    //  Value operator[](int index);
    //  Value operator[](std::string index);

    void toString(); // convert the value to a string from a symbol
    void toSymbol(); // convert the value to a symbol from a string

    Kind kind;
    bool bValue;
    long iValue;
    double fValue;
    std::string sValue; // used for strings and for symbols
    MachineInstance *cached_machine;

    Value *cached_value;
    int token_id;

    bool numeric() const;
    bool identical(const Value &other) const; // bypasses default == for floats

    DynamicValueBase *dynamicValue() const { return dyn_value; }
    void setDynamicValue();
    void setDynamicValue(DynamicValueBase *dv);
    void setDynamicValue(DynamicValueBase &dv);

    Value &operator=(const Value &orig);
    Value &operator=(bool);
    Value &operator=(int);
    Value &operator=(long);
    Value &operator=(unsigned long);
    Value &operator=(const char *);
    Value &operator=(std::string);
    Value &operator=(float);
    Value &operator=(double);

    bool operator>=(const Value &other) const;
    bool operator<=(const Value &other) const;
    bool operator<(const Value &other) const { return !operator>=(other); }
    bool operator>(const Value &other) const { return !operator<=(other); }
    bool operator==(const Value &other) const;
    bool operator!=(const Value &other) const;
    bool operator&&(const Value &other) const;
    bool operator||(const Value &other) const;
    bool operator!() const;
    Value operator-() const;
    Value operator+(const Value &other);
    Value &operator+=(const Value &other);
    Value operator-(const Value &other);
    Value &operator-=(const Value &other);
    Value operator*(const Value &other);
    Value &operator*=(const Value &other);
    Value operator/(const Value &other);
    Value &operator/=(const Value &other);
    Value operator%(const Value &other);
    Value &operator%=(const Value &other);
    Value operator&(const Value &other);
    Value &operator&=(const Value &other);
    Value operator|(const Value &other);
    Value &operator|=(const Value &other);
    Value operator^(const Value &other);
    Value &operator^=(const Value &other);
    Value &operator~();

    std::ostream &operator<<(std::ostream &out) const;

  private:
    DynamicValueBase *dyn_value;
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
    void push_back(Value *value) const {
        if (list) {
            list->push_back(value);
        }
    }
};
