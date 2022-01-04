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

#ifndef cwlang_dynamic_value_h
#define cwlang_dynamic_value_h

#include "Expression.h"
#include "value.h"
#include <iostream>
#include <list>
#include <map>
#include <string>

class MachineInstance;

class DynamicValue : public DynamicValueBase {
  public:
    DynamicValue() : scope(0), last_process_time(0) {}
    virtual ~DynamicValue() {}
    virtual const Value &operator()(MachineInstance *scope); // uses the provided machine's scope
    virtual const Value &operator()(); // uses the current scope for evaluation
    static DynamicValue *promote(DynamicValueBase *val) {
        return dynamic_cast<DynamicValue *>(val);
    }
    static const DynamicValue *promote(const DynamicValueBase *val) {
        return dynamic_cast<const DynamicValue *>(val);
    }
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    virtual const Value *lastResult() const { return &last_result; }
    void setScope(MachineInstance *m) { scope = m; }
    MachineInstance *getScope() const { return scope; }
    virtual void flushCache();

  protected:
    Value last_result;
    MachineInstance *scope;
    uint64_t last_process_time;
    DynamicValue(const DynamicValue &other) : last_result(other.last_result), scope(0) {}

  private:
    DynamicValue &operator=(const DynamicValue &other);
};
std::ostream &operator<<(std::ostream &, const DynamicValue &);

class AssignmentValue : public DynamicValue {
  public:
    AssignmentValue(const char *dest, Value *val) : src(*val), dest_name(dest) {}
    virtual ~AssignmentValue() {}
    virtual Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    AssignmentValue(const AssignmentValue &);

  private:
    AssignmentValue(const DynamicValue &);
    Value src;
    std::string dest_name;
};

class AnyInValue : public DynamicValue {
  public:
    AnyInValue(const char *state_str, const char *list)
        : state(state_str), machine_list_name(list), machine_list(0), state_property(0) {}
    virtual ~AnyInValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    AnyInValue(const AnyInValue &);

  private:
    AnyInValue(const DynamicValue &);
    std::string state;
    std::string machine_list_name;
    MachineInstance *machine_list;
    const Value *state_property;
};

class AllInValue : public DynamicValue {
  public:
    AllInValue(const char *state_str, const char *list)
        : state(state_str), machine_list_name(list), machine_list(0), state_property(0) {}
    virtual ~AllInValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    AllInValue(const AllInValue &);

  private:
    AllInValue(const DynamicValue &);
    std::string state;
    std::string machine_list_name;
    MachineInstance *machine_list;
    const Value *state_property;
};

class AnyEnabledDisabledValue : public DynamicValue {
  public:
    AnyEnabledDisabledValue(bool enabled, const char *list)
        : check_enabled(enabled), machine_list_name(list), machine_list(0) {}
    virtual ~AnyEnabledDisabledValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    AnyEnabledDisabledValue(const AnyEnabledDisabledValue &);

  private:
    AnyEnabledDisabledValue(const DynamicValue &);
    bool check_enabled;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class AllEnabledDisabledValue : public DynamicValue {
  public:
    AllEnabledDisabledValue(bool enabled, const char *list)
        : check_enabled(enabled), machine_list_name(list), machine_list(0) {}
    virtual ~AllEnabledDisabledValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    AllEnabledDisabledValue(const AllEnabledDisabledValue &);

  private:
    AllEnabledDisabledValue(const DynamicValue &);
    bool check_enabled;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class CountValue : public DynamicValue {
  public:
    CountValue(const char *state_str, const char *list)
        : state(state_str), machine_list_name(list), machine_list(0), state_property(0) {}
    virtual ~CountValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    CountValue(const CountValue &);

  private:
    CountValue(const DynamicValue &);
    std::string state;
    std::string machine_list_name;
    MachineInstance *machine_list;
    const Value *state_property;
};

class FindValue : public DynamicValue {
  public:
    FindValue(const char *list, Predicate *pred)
        : machine_list_name(list), machine_list(0), condition(pred) {}
    virtual ~FindValue() {}
    virtual Value &operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    FindValue(const FindValue &);

  private:
    FindValue(const DynamicValue &);
    std::string property_name;
    std::string machine_list_name;
    MachineInstance *machine_list;
    Condition condition;
};

class SumValue : public DynamicValue {
  public:
    SumValue(const char *property_name, const char *list)
        : property(property_name), machine_list_name(list), machine_list(0) {}
    SumValue(const char *list) : property(""), machine_list_name(list), machine_list(0) {}
    virtual ~SumValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    SumValue(const SumValue &);

  private:
    SumValue(const DynamicValue &);
    std::string property;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class MinValue : public DynamicValue {
  public:
    MinValue(const char *property_name, const char *list)
        : property(property_name), machine_list_name(list), machine_list(0) {}
    MinValue(const char *list) : property(""), machine_list_name(list), machine_list(0) {}
    virtual ~MinValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    MinValue(const MinValue &);

  private:
    MinValue(const DynamicValue &);
    std::string property;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class MaxValue : public DynamicValue {
  public:
    MaxValue(const char *property_name, const char *list)
        : property(property_name), machine_list_name(list), machine_list(0) {}
    MaxValue(const char *list) : property(""), machine_list_name(list), machine_list(0) {}
    virtual ~MaxValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    MaxValue(const MaxValue &);

  private:
    MaxValue(const DynamicValue &);
    std::string property;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class SerialiseValue : public DynamicValue {
  public:
    SerialiseValue(const char *property_name, const char *list, const char *delimeter)
        : property(property_name), machine_list_name(list), delim(delimeter), machine_list(0) {}
    SerialiseValue(const char *list, const char *delimeter)
        : property(""), machine_list_name(list), delim(delimeter), machine_list(0) {}
    virtual ~SerialiseValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    SerialiseValue(const SerialiseValue &);

  private:
    SerialiseValue(const DynamicValue &);
    std::string property;
    std::string machine_list_name;
    std::string delim;
    MachineInstance *machine_list;
};

class MeanValue : public DynamicValue {
  public:
    MeanValue(const char *property_name, const char *list)
        : property(property_name), machine_list_name(list), machine_list(0) {}
    MeanValue(const char *list) : property(""), machine_list_name(list), machine_list(0) {}
    virtual ~MeanValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    MeanValue(const MeanValue &);

  private:
    MeanValue(const DynamicValue &);
    std::string property;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class AbsoluteValue : public DynamicValue {
  public:
    AbsoluteValue(const char *property_name) : property(property_name) {}
    virtual ~AbsoluteValue() {}
    virtual Value &operator()(MachineInstance *mi);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    AbsoluteValue(const AbsoluteValue &);

  private:
    AbsoluteValue(const DynamicValue &);
    std::string property;
};

class ExpressionValue : public DynamicValue {
  public:
    ExpressionValue(Predicate *pred) : condition(pred) {}
    virtual ~ExpressionValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    ExpressionValue(const ExpressionValue &);

  private:
    ExpressionValue(const DynamicValue &);
    Condition condition;
};

class IncludesValue : public DynamicValue {
  public:
    IncludesValue(Value val, const char *list)
        : entry(val), machine_list_name(list), machine_list(0) {}
    virtual ~IncludesValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    IncludesValue(const IncludesValue &);

  private:
    IncludesValue(const DynamicValue &);
    Value entry;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class ItemAtPosValue : public DynamicValue {
  public:
    ItemAtPosValue(const char *list, Value *val, bool remove = true)
        : index(*val), machine_list_name(list), machine_list(0), remove_from_list(remove) {}
    virtual ~ItemAtPosValue() {}
    virtual Value &operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    ItemAtPosValue(const ItemAtPosValue &);

  private:
    ItemAtPosValue(const DynamicValue &);
    Value index;
    std::string machine_list_name;
    MachineInstance *machine_list;
    bool remove_from_list;
};

class SizeValue : public DynamicValue {
  public:
    SizeValue(const char *list) : machine_list_name(list), machine_list(0) {}
    virtual ~SizeValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    SizeValue(const SizeValue &);

  private:
    SizeValue(const DynamicValue &);
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class PopListBackValue : public DynamicValue {
  public:
    PopListBackValue(const char *list, bool remove = true)
        : machine_list_name(list), machine_list(0), remove_from_list(remove) {}
    virtual ~PopListBackValue() {}
    virtual Value &operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    PopListBackValue(const PopListBackValue &);

  private:
    PopListBackValue(const DynamicValue &);
    std::string machine_list_name;
    MachineInstance *machine_list;
    bool remove_from_list;
};

class PopListFrontValue : public DynamicValue {
  public:
    PopListFrontValue(const char *list, bool remove = true)
        : machine_list_name(list), machine_list(0), remove_from_list(remove) {}
    virtual ~PopListFrontValue() {}
    virtual Value &operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    PopListFrontValue(const PopListFrontValue &);

  private:
    PopListFrontValue(const DynamicValue &);
    std::string machine_list_name;
    MachineInstance *machine_list;
    bool remove_from_list;
};

class BitsetValue : public DynamicValue {
  public:
    BitsetValue(const char *state_str, const char *list)
        : state(state_str), machine_list_name(list), machine_list(0) {}
    virtual ~BitsetValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    BitsetValue(const BitsetValue &);

  private:
    BitsetValue(const DynamicValue &);
    std::string state;
    std::string machine_list_name;
    MachineInstance *machine_list;
};

class EnabledValue : public DynamicValue {
  public:
    EnabledValue(const char *name) : machine_name(name), machine(0) {}
    virtual ~EnabledValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    EnabledValue(const EnabledValue &);

  private:
    EnabledValue(const DynamicValue &);
    std::string machine_name;
    MachineInstance *machine;
};

class PropertyLookupValue : public DynamicValue {
  public:
    PropertyLookupValue(const char *machine, const char *property)
        : machine_name(machine), property_name(property), machine(0) {}
    virtual ~PropertyLookupValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    PropertyLookupValue(const PropertyLookupValue &);
    Value *getMutable(MachineInstance *scope);
    std::string resolvedName(MachineInstance *scope);

  private:
    PropertyLookupValue(const DynamicValue &);
    std::string machine_name;
    std::string property_name;
    MachineInstance *machine = nullptr;
};

class DisabledValue : public DynamicValue {
  public:
    DisabledValue(const char *name) : machine_name(name), machine(0) {}
    virtual ~DisabledValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    DisabledValue(const DisabledValue &);

  private:
    DisabledValue(const DynamicValue &);
    std::string machine_name;
    MachineInstance *machine;
};

class ExistsValue : public DynamicValue {
  public:
    ExistsValue(const char *name) : machine_name(name), machine(0) {}
    virtual ~ExistsValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    ExistsValue(const ExistsValue &);

  private:
    ExistsValue(const DynamicValue &);
    std::string machine_name;
    MachineInstance *machine;
};

class CastValue : public DynamicValue {
  public:
    CastValue(const char *name, const char *type_name) : property(name), kind(type_name) {}
    virtual ~CastValue() {}
    virtual Value &operator()(MachineInstance *);
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    CastValue(const CastValue &);

  private:
    CastValue(const DynamicValue &);
    std::string property;
    std::string kind;
};

class ClassNameValue : public DynamicValue {
  public:
    ClassNameValue(const char *name) : machine_name(name), machine(0) {}
    virtual ~ClassNameValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    ClassNameValue(const CastValue &);

  private:
    ClassNameValue(const DynamicValue &);
    std::string machine_name;
    MachineInstance *machine;
};

class ChangingStateValue : public DynamicValue {
  public:
    ChangingStateValue(const char *name) : machine_name(name), machine(0) {}
    virtual ~ChangingStateValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    ChangingStateValue(const CastValue &);

  private:
    ChangingStateValue(const DynamicValue &);
    std::string machine_name;
    MachineInstance *machine;
};

class AsFormattedStringValue : public DynamicValue {
  public:
    AsFormattedStringValue(const char *property, const char *fmt)
        : property_name(property), format(fmt) {}
    virtual ~AsFormattedStringValue() {}
    virtual const Value &operator()();
    virtual DynamicValue *clone() const;
    virtual std::ostream &operator<<(std::ostream &) const;
    AsFormattedStringValue(const AsFormattedStringValue &);

  private:
    AsFormattedStringValue(const DynamicValue &);
    std::string property_name;
    std::string format;
};

#endif
