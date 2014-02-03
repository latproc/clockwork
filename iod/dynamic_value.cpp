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
#include "MessageLog.h"


DynamicValue *DynamicValue::clone() const {
    return new DynamicValue(*this);
}

Value DynamicValue::operator()() {
    return SymbolTable::False;
}

Value DynamicValue::operator()(MachineInstance *m) {
    setScope(m);
    return operator()();
}

std::ostream &DynamicValue::operator<<(std::ostream &out ) const {
    return out << "<dynamic value> (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const DynamicValue &val) { return val.operator<<(out); }

DynamicValue *AssignmentValue::clone() const { return new AssignmentValue(*this); }
std::ostream &AssignmentValue::operator<<(std::ostream &out ) const {
    return out << dest_name << " BECOMES " << src << "(" << last_result <<")";
}
std::ostream &operator<<(std::ostream &out, const AssignmentValue &val) { return val.operator<<(out); }

DynamicValue *ItemAtPosValue::clone() const { return new ItemAtPosValue(*this); }
std::ostream &ItemAtPosValue::operator<<(std::ostream &out ) const {
    return out << "ITEM " << index << " OF " << machine_list_name << "(" << last_result <<")";
}
std::ostream &operator<<(std::ostream &out, const ItemAtPosValue &val) { return val.operator<<(out); }

DynamicValue *AnyInValue::clone() const {
    return new AnyInValue(*this);
}


std::ostream &AnyInValue::operator<<(std::ostream &out ) const {
    return out << "ANY " << machine_list_name << " ARE " << state << "(" << last_result <<")";
}
std::ostream &operator<<(std::ostream &out, const AnyInValue &val) { return val.operator<<(out); }


DynamicValue *AllInValue::clone() const { return new AllInValue(*this); }
std::ostream &AllInValue::operator<<(std::ostream &out ) const {
    return out << "ALL " << machine_list_name << " ARE " << state << "(" << last_result <<")";
}
std::ostream &operator<<(std::ostream &out, const AllInValue &val) { return val.operator<<(out); }

DynamicValue *CountValue::clone() const { return new CountValue(*this); }
std::ostream &CountValue::operator<<(std::ostream &out ) const {
    return out << "COUNT " << state << " FROM " << machine_list_name << " (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const CountValue &val) { return val.operator<<(out); }

DynamicValue *IncludesValue::clone() const { return new IncludesValue(*this); }
std::ostream &IncludesValue::operator<<(std::ostream &out ) const {
    return out << machine_list_name << " INCLUDES " << entry << "(" << last_result <<")";
}
std::ostream &operator<<(std::ostream &out, const IncludesValue &val) { return val.operator<<(out); }

DynamicValue *SizeValue::clone() const { return new SizeValue(*this); }
std::ostream &SizeValue::operator<<(std::ostream &out ) const {
    return out << "SIZE OF " << machine_list_name << "(" << last_result <<")";
}
std::ostream &operator<<(std::ostream &out, const SizeValue &val) { return val.operator<<(out); }

DynamicValue *BitsetValue::clone() const { return new BitsetValue(*this); }
std::ostream &BitsetValue::operator<<(std::ostream &out ) const {
    out << "BITSET FROM " << machine_list_name;
    if (!machine_list) out << "( no machine )";
    out << " (" << last_result <<")";
    return out;
}
std::ostream &operator<<(std::ostream &out, const BitsetValue &val) { return val.operator<<(out); }


AssignmentValue::AssignmentValue(const AssignmentValue &other) {
    src = other.src;
    dest_name = other.dest_name;
}
Value AssignmentValue::operator()() {
    MachineInstance *mi = getScope();
    if (src.kind == Value::t_symbol)
        last_result = mi->getValue(src.sValue);
    else
        last_result = src;
    mi->setValue(dest_name, last_result);
    return last_result;
}

AnyInValue::AnyInValue(const AnyInValue &other) {
    state = other.state;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}

Value AnyInValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL)
        machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_list_name << " for ANY IN "<<state<<" test\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false; return last_result;
    }
    for (unsigned int i=0; i<machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) mi->lookup(machine_list->parameters[i]);
        if (!machine_list->parameters[i].machine) continue;
        //std::cout << mi->getName() << " machine " << machine_list->parameters[i].machine->getName()
       // << " is  "<< machine_list->parameters[i].machine->getCurrent().getName() <<" want " << state << "\n";
        
        if (state == machine_list->parameters[i].machine->getCurrent().getName()) {
            last_result = true; return last_result;
        }
    }
    last_result = false; return last_result;
}


AllInValue::AllInValue(const AllInValue &other) {
    state = other.state;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}
Value AllInValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) {
        machine_list = mi->lookup(machine_list_name);
    }
    if (!machine_list) {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_list_name << " for ALL "<<state<<" test\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false; return last_result;
    }
    if (machine_list->parameters.size() == 0) {  last_result = false; return last_result; }
    for (unsigned int i=0; i<machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) mi->lookup(machine_list->parameters[i]);
        if (!machine_list->parameters[i].machine) continue;
        
        if (state != machine_list->parameters[i].machine->getCurrent().getName()) {
            last_result = false; return last_result;
        }
    }
    last_result = true;
    return last_result;
}


CountValue::CountValue(const CountValue &other) {
    state = other.state;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}

Value CountValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_list_name << " for COUNT "<<state<<" test\n";
        MessageLog::instance()->add(ss.str().c_str());
        return false;
    }
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

IncludesValue::IncludesValue(const IncludesValue &other) {
    entry = other.entry;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}

Value IncludesValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list)  {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_list_name << " for LIST operation\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false; return last_result;
    }
    for (unsigned int i=0; i<machine_list->parameters.size(); ++i) {
        if (entry == machine_list->parameters[i].val )  { last_result = true; return last_result; }
        if (entry.asString() == machine_list->parameters[i].real_name)  { last_result = true; return last_result; }
    }
    last_result = false; return last_result;
}

SizeValue::SizeValue(const SizeValue &other) {
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}

Value SizeValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list)  {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_list_name << " for SIZE test\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false; return last_result;
    }
    last_result = machine_list->parameters.size();
    return last_result;
}


PopListBackValue::PopListBackValue(const PopListBackValue &other) {
    machine_list_name = other.machine_list_name;
    machine_list = 0;
    remove_from_list = other.remove_from_list;
}

DynamicValue *PopListBackValue::clone() const { return new PopListBackValue(*this); }
std::ostream &PopListBackValue::operator<<(std::ostream &out ) const {
    if (remove_from_list)
        return out << " TAKE LAST FROM " << machine_list_name;
    else
        return out << " LAST OF " << machine_list_name<< " (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const PopListBackValue &val) { return val.operator<<(out); }


Value PopListBackValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list)  {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_list_name << " for LIST operation\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false; return last_result;
    }
    long i = machine_list->parameters.size() - 1;
    last_result = false;
    if (i>=0) {
        last_result = machine_list->parameters[i].val;
        if (remove_from_list){
            machine_list->parameters.pop_back();
            machine_list->setNeedsCheck();
        }
    }
    return last_result;
}

PopListFrontValue::PopListFrontValue(const PopListFrontValue &other) {
    machine_list_name = other.machine_list_name;
    machine_list = 0;
    remove_from_list = other.remove_from_list;
}
DynamicValue *PopListFrontValue::clone() const { return new PopListFrontValue(*this); }
std::ostream &PopListFrontValue::operator<<(std::ostream &out ) const {
    if (remove_from_list)
        return out << " TAKE FIRST FROM " << machine_list_name;
    else
        return out << " FIRST OF " << machine_list_name << " (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const PopListFrontValue &val) { return val.operator<<(out); }

Value PopListFrontValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list)  {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_list_name << " for LIST operation\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false; return last_result;
    }
    last_result = false;
    if (machine_list->_type == "REFERENECE") {
        if (machine_list->locals.size()) {
            last_result = machine_list->locals[0].val;
            if (machine_list->locals[0].machine && !last_result.cached_machine) {
                last_result.cached_machine = machine_list->locals[0].machine;
                std::string msg("Warning parameter with machine pointer was not completely configured: ");
                msg += last_result.asString();
                MessageLog::instance()->add(msg.c_str());
            }
            if (remove_from_list){
                machine_list->removeLocal(0);
                machine_list->setNeedsCheck();
            }
        }
    }
    else {
        if (machine_list->parameters.size()) {
            last_result = machine_list->parameters[0].val;
            if (machine_list->parameters[0].machine && !last_result.cached_machine) {
                last_result.cached_machine = machine_list->parameters[0].machine;
                std::string msg("Warning parameter with machine pointer was not completely configured: ");
                msg += last_result.asString();
                MessageLog::instance()->add(msg.c_str());
            }
            if (remove_from_list){
                machine_list->parameters.erase(machine_list->parameters.begin());
                if (machine_list->_type == "LIST") {
                    machine_list->setNeedsCheck();
                }
            }
        }
    }
    return last_result;
}


ItemAtPosValue::ItemAtPosValue(const ItemAtPosValue &other) {
    machine_list_name = other.machine_list_name;
    machine_list = 0;
    remove_from_list = other.remove_from_list;
}
Value ItemAtPosValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list)  {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_list_name << " for LIST operation\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false; return last_result;
    }
    if (machine_list->parameters.size()) {
        long idx = -1;
        if (index.kind == Value::t_symbol) {
            if (!mi->getValue(index.sValue).asInteger(idx)) {
                MessageLog::instance()->add("non-numeric index when evaluating ITEM AT pos");
                last_result = false; return last_result;
            }
        }
        else if (index.kind == Value::t_integer) {
            idx = index.iValue;
        }
        else {
            if (!index.asInteger(idx)) {
                MessageLog::instance()->add("non-numeric index when evaluating ITEM AT pos");
                last_result = false;
                return last_result;
            }
        }
        if (idx>=0 && idx < machine_list->parameters.size()) {
            last_result = machine_list->parameters[idx].val;
            if (remove_from_list) {
                machine_list->parameters.erase(machine_list->parameters.begin()+idx);
                if (machine_list->_type == "LIST") {
                    machine_list->setNeedsCheck();
                }
            }
            return last_result;
        }
    }
    
    last_result = false; return last_result;
}


BitsetValue::BitsetValue(const BitsetValue &other) {
    machine_list_name = other.machine_list_name;
    machine_list = 0;
    state = other.state;
}
Value BitsetValue::operator()(MachineInstance *mi) {
    if (machine_list == NULL) machine_list = mi->lookup(machine_list_name);
    if (!machine_list)  {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_list_name << " for LIST operation\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false; return last_result;
    }
    unsigned long val = 0;
    for (unsigned int i=0; i<machine_list->parameters.size(); ++i) {
        MachineInstance *entry = machine_list->parameters[i].machine;
        val *= 2;
        if (entry) {
            if (state == entry->getCurrentStateString())  {
                val += 1;
            }
        }
    }
    last_result = val;
    return last_result;
}


EnabledValue::EnabledValue(const EnabledValue &other) {
    machine_name = other.machine_name;
    machine = 0;
}
DynamicValue *EnabledValue::clone() const { return new EnabledValue(*this); }
Value EnabledValue::operator()(MachineInstance *mi) {
    if (machine == NULL) machine = mi->lookup(machine_name);
    if (!machine)  {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_name << " for ENABLED test\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false; return last_result;
    }
    last_result = machine->enabled();
    return last_result;
}
std::ostream &EnabledValue::operator<<(std::ostream &out ) const {
    return out << machine_name << " ENABLED ";
}
std::ostream &operator<<(std::ostream &out, const EnabledValue &val) { return val.operator<<(out); }


DisabledValue::DisabledValue(const DisabledValue &other) {
    machine_name = other.machine_name;
    machine = 0;
}
DynamicValue *DisabledValue::clone() const { return new DisabledValue(*this); }
Value DisabledValue::operator()(MachineInstance *mi) {
    if (machine == NULL) machine = mi->lookup(machine_name);
    if (!machine)  {
        std::stringstream ss; ss << mi->getName() << " no machine " << machine_name << " for DISABLED test\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false; return last_result;
    }
    last_result = !machine->enabled();
    return last_result;
}
std::ostream &DisabledValue::operator<<(std::ostream &out ) const {
    return out << machine_name << " DISABLED ";
}
std::ostream &operator<<(std::ostream &out, const DisabledValue &val) { return val.operator<<(out); }


CastValue::CastValue(const CastValue &other) {
    property = other.property;
    kind = other.kind;
}
DynamicValue *CastValue::clone() const { return new CastValue(*this); }
Value CastValue::operator()(MachineInstance *mi) {
    Value val = mi->properties.lookup(property.c_str());
    if (kind == "STRING")
        last_result = val.asString();
    else if (kind == "NUMBER") {
        long lValue = 0;
        if (val.asInteger(lValue))
            last_result = lValue;
        else
            last_result = false;
        return last_result;
    }
    return last_result;
}
std::ostream &CastValue::operator<<(std::ostream &out ) const {
    return out << "CAST(" << property << "," << kind << ") ";
}
std::ostream &operator<<(std::ostream &out, const CastValue &val) { return val.operator<<(out); }



