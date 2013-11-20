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
#include "MachineInstance.h"
#include "dynamic_value.h"


DynamicValue *DynamicValue::clone() const { return new DynamicValue(); }


DynamicValue *AnyInValue::clone() const { return new AnyInValue(*this); }
std::ostream &AnyInValue::operator<<(std::ostream &out ) const {
    return out << "ANY " << machine_list_name << " IN " << state;
}
std::ostream &operator<<(std::ostream &out, const AnyInValue &val) { return val.operator<<(out); }


DynamicValue *AllInValue::clone() const { return new AllInValue(*this); }
std::ostream &AllInValue::operator<<(std::ostream &out ) const {
    return out << "ALL " << machine_list_name << " ARE " << state;
}
std::ostream &operator<<(std::ostream &out, const AllInValue &val) { return val.operator<<(out); }

DynamicValue *CountValue::clone() const { return new CountValue(*this); }
std::ostream &CountValue::operator<<(std::ostream &out ) const {
    return out << "COUNT " << state << " FROM " << machine_list_name << " (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const CountValue &val) { return val.operator<<(out); }

DynamicValue *IncludesValue::clone() const { return new IncludesValue(*this); }
std::ostream &IncludesValue::operator<<(std::ostream &out ) const {
    return out << machine_list_name << " INCLUDES " << entry_name;
}
std::ostream &operator<<(std::ostream &out, const IncludesValue &val) { return val.operator<<(out); }

DynamicValue *BitsetValue::clone() const { return new BitsetValue(*this); }
std::ostream &BitsetValue::operator<<(std::ostream &out ) const {
    return out << "BITSET FROM " << machine_list << state;
}
std::ostream &operator<<(std::ostream &out, const BitsetValue &val) { return val.operator<<(out); }

Value DynamicValue::operator()(MachineInstance*) { return SymbolTable::False; }

Value AnyInValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list) { last_result = false; return last_result; }
    for (unsigned int i=0; i<machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) mi->lookup(machine_list->parameters[i]);
        if (!machine_list->parameters[i].machine) continue;
        
        if (state == machine_list->parameters[i].machine->getCurrent().getName()) { last_result = true; return last_result; }
    }
    last_result = false; return last_result;
}
Value AllInValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list) { last_result = false; return last_result; }
    if (machine_list->parameters.size() == 0) { last_result = false; return last_result; }
    for (unsigned int i=0; i<machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) mi->lookup(machine_list->parameters[i]);
        if (!machine_list->parameters[i].machine) continue;
        
        if (state != machine_list->parameters[i].machine->getCurrent().getName()) { last_result = false; return last_result; }
    }
    last_result = true; return last_result;
}

Value CountValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list) return false;
    if (machine_list->parameters.size() == 0) return 0;
    int result = 0;
    for (unsigned int i=0; i<machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) mi->lookup(machine_list->parameters[i]);
        if (!machine_list->parameters[i].machine) continue;
        
        if (state == machine_list->parameters[i].machine->getCurrent().getName()) ++result;
    }
    last_result = result;
    return last_result;
}

Value IncludesValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list)  { last_result = false; return last_result; }
    for (unsigned int i=0; i<machine_list->parameters.size(); ++i) {
        if (entry_name == machine_list->parameters[i].val.asString() || entry_name == machine_list->parameters[i].real_name)  { last_result = true; return last_result; }
    }
    last_result = false; return last_result;
}

Value BitsetValue::operator()(MachineInstance *mi) {
    last_result = SymbolTable::Null;
    return last_result;
}

