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
DynamicValue *AllInValue::clone() const { return new AllInValue(*this); }
DynamicValue *CountValue::clone() const { return new CountValue(*this); }
DynamicValue *IncludesValue::clone() const { return new IncludesValue(*this); }
DynamicValue *BitsetValue::clone() const { return new BitsetValue(*this); }

Value DynamicValue::operator()(MachineInstance*) { return SymbolTable::False; }

Value AnyInValue::operator()() {
    return SymbolTable::False; }
Value AllInValue::operator()() {
    return SymbolTable::False; }
Value CountValue::operator()() {
    return SymbolTable::Zero; }

Value IncludesValue::operator()() {
    return SymbolTable::False;
}


Value IncludesValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list) return SymbolTable::False;
    for (unsigned int i=0; i<machine_list->parameters.size(); ++i) {
        if (entry_name == machine_list->parameters[i].val.asString() || entry_name == machine_list->parameters[i].real_name) return true;
    }
    return false;
}
Value BitsetValue::operator()() {
    return SymbolTable::Null;
}

