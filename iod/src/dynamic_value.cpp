#include "dynamic_value.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "regular_expressions.h"
#include "symboltable.h"
#include <boost/foreach.hpp>
#include <functional>
#include <iostream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <utility>

namespace {

void LogMissingMachine(MachineInstance *current_scope, const std::string &name,
                       const std::string source) {
    std::stringstream ss;
    ss << current_scope->getName() << " no machine " << name << " " << source;
    MessageLog::instance()->add(ss.str().c_str());
}

} // namespace

static uint64_t currentTime() { return microsecs(); }

DynamicValue *DynamicValue::clone() const { return new DynamicValue(*this); }

const Value &DynamicValue::operator()() { return SymbolTable::False; }

void DynamicValue::flushCache() {
    last_result = SymbolTable::Null;
    last_process_time = 0;
}

const Value &DynamicValue::operator()(MachineInstance *m) {
    if (scope != m) {
        setScope(m);
    }
    return operator()();
}

std::ostream &DynamicValue::operator<<(std::ostream &out) const {
    return out << "<dynamic value> (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const DynamicValue &val) { return val.operator<<(out); }

DynamicValue *AssignmentValue::clone() const { return new AssignmentValue(*this); }
std::ostream &AssignmentValue::operator<<(std::ostream &out) const {
    return out << dest_name << " BECOMES " << src << "(" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const AssignmentValue &val) {
    return val.operator<<(out);
}

DynamicValue *ItemAtPosValue::clone() const { return new ItemAtPosValue(*this); }
std::ostream &ItemAtPosValue::operator<<(std::ostream &out) const {
    return out << "ITEM " << index << " OF " << machine_list_name << "(" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const ItemAtPosValue &val) {
    return val.operator<<(out);
}

DynamicValue *AnyInValue::clone() const { return new AnyInValue(*this); }

std::ostream &AnyInValue::operator<<(std::ostream &out) const {
    return out << "ANY " << machine_list_name << " ARE " << state << "(" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const AnyInValue &val) { return val.operator<<(out); }

DynamicValue *AllInValue::clone() const { return new AllInValue(*this); }
std::ostream &AllInValue::operator<<(std::ostream &out) const {
    return out << "ALL " << machine_list_name << " ARE " << state << "(" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const AllInValue &val) { return val.operator<<(out); }

DynamicValue *AnyEnabledDisabledValue::clone() const { return new AnyEnabledDisabledValue(*this); }

std::ostream &AnyEnabledDisabledValue::operator<<(std::ostream &out) const {
    return out << "ANY " << machine_list_name << ((check_enabled) ? " ENABLED" : " DISABLED") << "("
               << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const AnyEnabledDisabledValue &val) {
    return val.operator<<(out);
}

DynamicValue *AllEnabledDisabledValue::clone() const { return new AllEnabledDisabledValue(*this); }
std::ostream &AllEnabledDisabledValue::operator<<(std::ostream &out) const {
    return out << "ALL " << machine_list_name << ((check_enabled) ? " ENABLED" : " DISABLED") << "("
               << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const AllEnabledDisabledValue &val) {
    return val.operator<<(out);
}

DynamicValue *CountValue::clone() const { return new CountValue(*this); }
std::ostream &CountValue::operator<<(std::ostream &out) const {
    return out << "COUNT " << state << " FROM " << machine_list_name << " (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const CountValue &val) { return val.operator<<(out); }

DynamicValue *FindValue::clone() const { return new FindValue(*this); }
std::ostream &FindValue::operator<<(std::ostream &out) const {
    return out << "INDEX OF ITEM IN " << machine_list_name << " WHERE " << *condition.predicate
               << " (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const FindValue &val) { return val.operator<<(out); }

std::ostream &display_list_op(std::ostream &out, const char *op, const std::string &property,
                              const std::string &machine_list_name, const Value &last_result) {
    return out << op << " ";
    if (property.empty()) {
        out << " OF ";
    }
    else {
        out << property << " FROM ";
    }
    return out << machine_list_name << " (" << last_result << ")";
}

DynamicValue *SumValue::clone() const { return new SumValue(*this); }
std::ostream &SumValue::operator<<(std::ostream &out) const {
    return display_list_op(out, "SUM", property, machine_list_name, last_result);
}
std::ostream &operator<<(std::ostream &out, const SumValue &val) { return val.operator<<(out); }

DynamicValue *SerialiseValue::clone() const { return new SerialiseValue(*this); }
std::ostream &SerialiseValue::operator<<(std::ostream &out) const {
    out << "SERIALISE ";
    if (!property.empty()) {
        out << property << " FROM ";
    }
    return out << machine_list_name << " SEPARATED BY \"" << delim << "\" (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const SerialiseValue &val) {
    return val.operator<<(out);
}

DynamicValue *MinValue::clone() const { return new MinValue(*this); }
std::ostream &MinValue::operator<<(std::ostream &out) const {
    return display_list_op(out, "MIN", property, machine_list_name, last_result);
}
std::ostream &operator<<(std::ostream &out, const MinValue &val) { return val.operator<<(out); }

DynamicValue *MaxValue::clone() const { return new MaxValue(*this); }
std::ostream &MaxValue::operator<<(std::ostream &out) const {
    return display_list_op(out, "MAX", property, machine_list_name, last_result);
}
std::ostream &operator<<(std::ostream &out, const MaxValue &val) { return val.operator<<(out); }

DynamicValue *MeanValue::clone() const { return new MeanValue(*this); }
std::ostream &MeanValue::operator<<(std::ostream &out) const {
    return display_list_op(out, "MEAN", property, machine_list_name, last_result);
}
std::ostream &operator<<(std::ostream &out, const MeanValue &val) { return val.operator<<(out); }

DynamicValue *AbsoluteValue::clone() const { return new AbsoluteValue(*this); }
std::ostream &AbsoluteValue::operator<<(std::ostream &out) const {
    return out << "ABS" << property << " (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const AbsoluteValue &val) {
    return val.operator<<(out);
}

DynamicValue *ExpressionValue::clone() const { return new ExpressionValue(*this); }
std::ostream &ExpressionValue::operator<<(std::ostream &out) const {
    return out << *(condition.predicate) << " (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const ExpressionValue &val) {
    return val.operator<<(out);
}

DynamicValue *IncludesValue::clone() const { return new IncludesValue(*this); }
std::ostream &IncludesValue::operator<<(std::ostream &out) const {
    return out << machine_list_name << " INCLUDES " << entry << "(" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const IncludesValue &val) {
    return val.operator<<(out);
}

DynamicValue *SizeValue::clone() const { return new SizeValue(*this); }
std::ostream &SizeValue::operator<<(std::ostream &out) const {
    return out << "SIZE OF " << machine_list_name << "(" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const SizeValue &val) { return val.operator<<(out); }

DynamicValue *BitsetValue::clone() const { return new BitsetValue(*this); }
std::ostream &BitsetValue::operator<<(std::ostream &out) const {
    out << "BITSET FROM " << machine_list_name;
    if (!machine_list) {
        out << "( no machine )";
    }
    out << " (" << last_result << ")";
    return out;
}
std::ostream &operator<<(std::ostream &out, const BitsetValue &val) { return val.operator<<(out); }

PatternMatchValue::PatternMatchValue(Predicate *pattern, const char *prop)
    : pattern(pattern), property_name(prop), separator(";", Value::t_string) {}
PatternMatchValue::PatternMatchValue(Predicate *pattern, const char *prop, const Value &sep)
    : pattern(pattern), property_name(prop), separator(sep) {}
DynamicValue *PatternMatchValue::clone() const { return new PatternMatchValue(*this); }
std::ostream &PatternMatchValue::operator<<(std::ostream &out) const {
    out << "MATCHES OF " << pattern << " IN " << property_name << " SEPARATED BY " << separator;
    return out;
}

AssignmentValue::AssignmentValue(const AssignmentValue &other) {
    src = other.src;
    dest_name = other.dest_name;
}
Value &AssignmentValue::operator()() {
    MachineInstance *mi = getScope();
    if (src.kind == Value::t_symbol) {
        last_result = mi->getValue(src.sValue);
        if (last_result == SymbolTable::Null) {
            std::cerr << "assignment of " << src.sValue << " yielded a null\n";
        }
        else {
            std::cerr << "assigning: " << last_result << " (" << last_result.kind << ") to "
                      << dest_name << "\n";
        }
    }
    else {
        last_result = src;
    }
    mi->setValue(dest_name, last_result);
    return last_result;
}

AnyInValue::AnyInValue(const AnyInValue &other) {
    state = other.state;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
    state_property = 0;
}

const Value &AnyInValue::operator()() {
    MachineInstance *mi = scope;
    if (state_property == 0) {
        state_property = &mi->getValue(state.c_str());
    }
    if (state_property == 0) {
        char buf[150];
        snprintf(buf, 150, "%s cannot find state %s", mi->getName().c_str(), state.c_str());
        MessageLog::instance()->add(buf);
        NB_MSG << buf << "\n";
    }

    machine_list = mi->lookup(machine_list_name);

    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for ANY IN %s", mi->getName().c_str(),
                 machine_list_name.c_str(), state.c_str());
        MessageLog::instance()->add(buf);
        last_result = false;
        return last_result;
    }

#if 0
    if (last_process_time > mi->lastStateEvaluationTime()) {
        //DBG_MSG << "avoiding recalc of " << *this << " last_process_time: "
        //  << last_process_time << " last list eval: " << machine_list->lastStateEvaluationTime() << "\n";
        return last_result;
    }
#endif

    last_process_time = currentTime();
    std::string state_val = state;
    if (state_property && state_property != &SymbolTable::Null) {
        state_val = state_property->asString();
    }
    for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) {
            mi->lookup(machine_list->parameters[i]);
        }
        if (!machine_list->parameters[i].machine) {
            continue;
        }
        //std::cout << mi->getName() << " machine " << machine_list->parameters[i].machine->getName()
        //<< " is  "<< machine_list->parameters[i].machine->getCurrent().getName() <<" want " << state_val << "\n";

        if (state_val == machine_list->parameters[i].machine->getCurrent().getName()) {
            last_result = true;
            return last_result;
        }
    }
    last_result = false;
    return last_result;
}

AllInValue::AllInValue(const AllInValue &other) {
    state = other.state;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
    state_property = 0;
}
const Value &AllInValue::operator()() {
    MachineInstance *mi = scope;
    if (state_property == 0) {
        state_property = &mi->getValue(state.c_str());
    }
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for ALL %s", mi->getName().c_str(),
                 machine_list_name.c_str(), state.c_str());
        MessageLog::instance()->add(buf);
        last_result = false;
        return last_result;
    }

#if 0
    if (last_process_time > mi->lastStateEvaluationTime()) {
        //std::cout << "avoiding recalc of " << *this << "\n";
        return last_result;
    }
#endif

    last_process_time = currentTime();
    if (machine_list->parameters.size() == 0) {
        last_result = false;
        return last_result;
    }

    std::string state_val = state;
    if (state_property && state_property != &SymbolTable::Null) {
        state_val = state_property->asString();
    }
    for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) {
            mi->lookup(machine_list->parameters[i]);
        }
        if (!machine_list->parameters[i].machine) {
            continue;
        }

        if (state_val != machine_list->parameters[i].machine->getCurrent().getName()) {
            last_result = false;
            return last_result;
        }
    }
    last_result = true;
    return last_result;
}

AnyEnabledDisabledValue::AnyEnabledDisabledValue(const AnyEnabledDisabledValue &other) {
    check_enabled = other.check_enabled;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}

const Value &AnyEnabledDisabledValue::operator()() {
    MachineInstance *mi = scope;
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for ANY %s", mi->getName().c_str(),
                 machine_list_name.c_str(), (check_enabled) ? "ENABLED" : "DISABLED");
        MessageLog::instance()->add(buf);
        last_result = false;
        return last_result;
    }

    last_process_time = currentTime();
    for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) {
            mi->lookup(machine_list->parameters[i]);
        }
        if (!machine_list->parameters[i].machine) {
            continue;
        }

        if (check_enabled == machine_list->parameters[i].machine->enabled()) {
            last_result = true;
            return last_result;
        }
    }
    last_result = false;
    return last_result;
}

AllEnabledDisabledValue::AllEnabledDisabledValue(const AllEnabledDisabledValue &other) {
    check_enabled = other.check_enabled;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}

const Value &AllEnabledDisabledValue::operator()() {
    MachineInstance *mi = scope;
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for ALL %s", mi->getName().c_str(),
                 machine_list_name.c_str(), (check_enabled) ? "ENABLED" : "DISABLED");
        MessageLog::instance()->add(buf);
        last_result = false;
        return last_result;
    }

    last_process_time = currentTime();
    if (machine_list->parameters.size() == 0) {
        last_result = false;
        return last_result;
    }
    for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) {
            mi->lookup(machine_list->parameters[i]);
        }
        if (!machine_list->parameters[i].machine) {
            continue;
        }

        if (check_enabled != machine_list->parameters[i].machine->enabled()) {
            last_result = false;
            return last_result;
        }
    }
    last_result = true;
    return last_result;
}

CountValue::CountValue(const CountValue &other) {
    state = other.state;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
    state_property = 0;
}

const Value &CountValue::operator()() {
    MachineInstance *mi = scope;
    if (state_property == 0) {
        state_property = &mi->getValue(state.c_str());
    }
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for count %s", mi->getName().c_str(),
                 machine_list_name.c_str(), state.c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }

#if 0
    if (last_process_time > machine_list->lastStateEvaluationTime()) {
        return last_result;
    }
#endif

    last_process_time = currentTime();
    if (machine_list->parameters.size() == 0) {
        last_result = 0;
        return last_result;
    }

    std::string state_val = state;
    if (state_property != &SymbolTable::Null) {
        state_val = state_property->asString();
    }
    int result = 0;
    for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) {
            mi->lookup(machine_list->parameters[i]);
        }
        if (!machine_list->parameters[i].machine) {
            continue;
        }

        if (state_val == machine_list->parameters[i].machine->getCurrent().getName()) {
            ++result;
        }
    }
    last_result = result;
    return last_result;
}

FindValue::FindValue(const FindValue &other) {
    property_name = other.property_name;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
    condition = other.condition;
}

Value &FindValue::operator()(MachineInstance *scope) {
    machine_list = scope->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for index search %s", scope->getName().c_str(),
                 machine_list_name.c_str(), property_name.c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }

    last_process_time = currentTime();
    if (machine_list->parameters.size() == 0) {
        last_result = -1;
        return last_result;
    }

    std::string prop_val;
    int result = -1;
    // find or create the index to be used for the ITEM reference
    bool keep_item = false; // true if this list had an 'ITEM'
    bool add_item = true;
    unsigned int idx = 0;
    while (idx < machine_list->locals.size()) {
        if (machine_list->locals[idx].val.asString() == "ITEM") {
            keep_item = true;
            add_item = false; // no need to add an item
            break;
        }
        ++idx;
    }

    for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
        Value a(machine_list->parameters.at(i).val);
        MachineInstance *mi = machine_list->parameters.at(i).machine;
        if (!mi) {
            mi = scope->lookup(machine_list->parameters[i]);
        }
        if (!mi) {
            continue;
        }

        // assign ITEM for the test
        if (add_item) {
            machine_list->locals.push_back(a);
            add_item = false;
        }
        else {
            machine_list->locals[idx] = a;
        }

        machine_list->locals[idx].machine = mi;
        machine_list->locals[idx].val.cached_machine = mi;

        machine_list->locals[idx].val = Value("ITEM");
        machine_list->locals[idx].real_name = a.sValue;

        if (condition.predicate) {
            condition.predicate->flushCache();
            machine_list->localised_names["ITEM"] = mi;
        }

        if ((!condition.predicate || condition(scope))) {
            result = i;
            break;
        }
    }
    if (!keep_item && machine_list->locals.size() > idx) {
        machine_list->locals.erase(machine_list->locals.begin() + idx);
    }

    last_result = result;
    return last_result;
}

SumValue::SumValue(const SumValue &other) {
    property = other.property;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}

const Value &SumValue::operator()() {
    MachineInstance *mi = scope;
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for sum %s", mi->getName().c_str(),
                 machine_list_name.c_str(), property.c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }
    if (machine_list->_type != "LIST") {
        char buf[400];
        snprintf(buf, 400,
                 "%s: attempting to calculate a SUM from a machine (%s) that is not a LIST",
                 mi->getName().c_str(), machine_list->fullName().c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }

    last_process_time = currentTime();
    if (machine_list->parameters.size() == 0) {
        last_result = 0;
        return last_result;
    }

    Value sum(0);
    if (!property.empty()) {
        for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
            if (!machine_list->parameters[i].machine) {
                mi->lookup(machine_list->parameters[i]);
            }
            if (!machine_list->parameters[i].machine) {
                continue;
            }

            sum = sum + machine_list->parameters[i].machine->getValue(property);
        }
    }
    else {
        for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
            sum = sum + machine_list->parameters[i].val;
        }
    }
    last_result = sum;
    return last_result;
}

SerialiseValue::SerialiseValue(const SerialiseValue &other) {
    property = other.property;
    machine_list_name = other.machine_list_name;
    delim = other.delim;
    machine_list = 0;
}

const Value &SerialiseValue::operator()() {
    MachineInstance *mi = scope;
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for concatenation %s", mi->getName().c_str(),
                 machine_list_name.c_str(), property.c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }
    if (machine_list->_type != "LIST") {
        char buf[400];
        snprintf(buf, 400, "%s: attempting to concatenate from a machine (%s) that is not a LIST",
                 mi->getName().c_str(), machine_list->fullName().c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }

    last_process_time = currentTime();
    if (machine_list->parameters.size() == 0) {
        last_result = 0;
        return last_result;
    }

    std::stringstream ss;
    for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
        Value val;
        if (!property.empty() || machine_list->parameters[i].val.kind == Value::t_symbol) {
            if (!machine_list->parameters[i].machine) {
                mi->lookup(machine_list->parameters[i]);
            }
            if (!machine_list->parameters[i].machine) {
                continue;
            }

            val = machine_list->parameters[i].machine->getValue(property.empty() ? "VALUE"
                                                                                 : property);
        }
        else {
            val = machine_list->parameters[i].val;
        }
        if (val.kind == Value::t_string) {
            val.toSymbol();
        }
        ss << val;
        if (i < machine_list->parameters.size() - 1) {
            ss << delim;
        }
    }
    last_result = ss.str();
    last_result.toString();
    return last_result;
}

MeanValue::MeanValue(const MeanValue &other) {
    property = other.property;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}

const Value &MeanValue::operator()() {
    MachineInstance *mi = scope;
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for sum %s", mi->getName().c_str(),
                 machine_list_name.c_str(), property.c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }
    if (machine_list->_type != "LIST") {
        char buf[400];
        snprintf(buf, 400,
                 "%s: attempting to calculate MEAN from a machine (%s) that is not a LIST",
                 mi->getName().c_str(), machine_list->fullName().c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }

    last_process_time = currentTime();
    if (machine_list->parameters.size() == 0) {
        last_result = 0;
        return last_result;
    }

    Value sum(0);
    int n = 0;
    last_result = 0;
    if (!property.empty()) {
        for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
            if (!machine_list->parameters[i].machine) {
                mi->lookup(machine_list->parameters[i]);
            }
            if (!machine_list->parameters[i].machine) {
                continue;
            }

            sum = sum + machine_list->parameters[i].machine->getValue(property);
            ++n;
        }
    }
    else {
        for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
            sum = sum + machine_list->parameters[i].val;
            ++n;
        }
    }
    if (n == 0) {
        return last_result;
    }
    double result;
    if (sum.asFloat(result)) {
        last_result = result / n;
    }
    return last_result;
}

MinValue::MinValue(const MinValue &other)
    : property(other.property), machine_list_name(other.machine_list_name), machine_list(nullptr) {}

const Value &MinValue::operator()() {
    MachineInstance *mi = scope;
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for min %s", mi->getName().c_str(),
                 machine_list_name.c_str(), property.c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }
    if (machine_list->_type != "LIST") {
        char buf[400];
        snprintf(buf, 400, "%s: attempting to find MIN from a machine (%s) that is not a LIST",
                 mi->getName().c_str(), machine_list->fullName().c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }

    last_process_time = currentTime();
    if (machine_list->parameters.size() == 0) {
        last_result = 0;
        return last_result;
    }

    Value min(LONG_MAX);
    bool unassigned = true;
    if (!property.empty()) {
        for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
            if (!machine_list->parameters[i].machine) {
                mi->lookup(machine_list->parameters[i]);
            }
            if (!machine_list->parameters[i].machine) {
                continue;
            }

            const Value &val = machine_list->parameters[i].machine->getValue(property);
            if (unassigned || val < min) {
                min = val;
                unassigned = false;
            }
        }
    }
    else {
        for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
            const Value &val = machine_list->parameters[i].val;
            if (unassigned || val < min) {
                min = val;
                unassigned = false;
            }
        }
    }
    last_result = min;
    return last_result;
}

MaxValue::MaxValue(const MaxValue &other)
    : property(other.property), machine_list_name(other.machine_list_name), machine_list(nullptr) {}

const Value &MaxValue::operator()() {
    MachineInstance *mi = scope;
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for min %s", mi->getName().c_str(),
                 machine_list_name.c_str(), property.c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }
    if (machine_list->_type != "LIST") {
        char buf[400];
        snprintf(buf, 400, "%s: attempting to find MAX from a machine (%s) that is not a LIST",
                 mi->getName().c_str(), machine_list->fullName().c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }

    last_process_time = currentTime();
    if (machine_list->parameters.size() == 0) {
        last_result = 0;
        return last_result;
    }

    Value max(LONG_MIN);
    bool unassigned = true;
    if (!property.empty()) {
        for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
            if (!machine_list->parameters[i].machine) {
                mi->lookup(machine_list->parameters[i]);
            }
            if (!machine_list->parameters[i].machine) {
                continue;
            }

            const Value &val = machine_list->parameters[i].machine->getValue(property);
            if (unassigned || val > max) {
                max = val;
                unassigned = false;
            }
        }
    }
    else {
        for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
            const Value &val = machine_list->parameters[i].val;
            if (unassigned || val > max) {
                max = val;
                unassigned = false;
            }
        }
    }
    last_result = max;
    return last_result;
}

ExpressionValue::ExpressionValue(const ExpressionValue &other) { condition = other.condition; }

const Value &ExpressionValue::operator()() {
    MachineInstance *mi = scope;
    last_process_time = currentTime();

    last_result = condition(mi);
    return last_result;
}

AbsoluteValue::AbsoluteValue(const AbsoluteValue &other) { property = other.property; }
Value &AbsoluteValue::operator()(MachineInstance *mi) {
    last_result = mi->getValue(property);
    if (last_result < 0) {
        last_result = -mi->getValue(property);
    }
    return last_result;
}

IncludesValue::IncludesValue(const IncludesValue &other) {
    entry = other.entry;
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}

const Value &IncludesValue::operator()() {
    MachineInstance *mi = scope;
    if (machine_list == NULL) {
        machine_list = mi->lookup(machine_list_name);
    }
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for INCLUDES", mi->getName().c_str(),
                 machine_list_name.c_str());
        MessageLog::instance()->add(buf);
        last_result = false;
        return last_result;
    }

#if 0
    if (last_process_time > machine_list->lastStateEvaluationTime()) {
        return last_result;
    }
#endif

    last_process_time = currentTime();
    for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
        if (!machine_list->parameters[i].machine) {
            mi->lookup(machine_list->parameters[i]);
        }
        if (entry == machine_list->parameters[i].val) {
            last_result = true;
            return last_result;
        }
        if (entry.asString() == machine_list->parameters[i].real_name) {
            last_result = true;
            return last_result;
        }
    }
    last_result = false;
    return last_result;
}

SizeValue::SizeValue(const SizeValue &other) {
    machine_list_name = other.machine_list_name;
    machine_list = 0;
}

const Value &SizeValue::operator()() {
    if (!scope) {
        return SymbolTable::Null;
    }
    machine_list = scope->lookup(machine_list_name);
    if (!machine_list) {
        char buf[400];
        snprintf(buf, 400, "%s: no machine %s for SIZE test", scope->getName().c_str(),
                 machine_list_name.c_str());
        MessageLog::instance()->add(buf);
        last_result = 0;
        return last_result;
    }

#if 0
    if (last_process_time > machine_list->lastStateEvaluationTime()) {
        return last_result;
    }
#endif

    last_process_time = currentTime();
    last_result = (long)machine_list->parameters.size();
    return last_result;
}

PopListBackValue::PopListBackValue(const PopListBackValue &other) {
    machine_list_name = other.machine_list_name;
    machine_list = 0;
    remove_from_list = other.remove_from_list;
}

DynamicValue *PopListBackValue::clone() const { return new PopListBackValue(*this); }
std::ostream &PopListBackValue::operator<<(std::ostream &out) const {
    if (remove_from_list) {
        return out << " TAKE LAST FROM " << machine_list_name;
    }
    else {
        return out << " LAST OF " << machine_list_name << " (" << last_result << ")";
    }
}
std::ostream &operator<<(std::ostream &out, const PopListBackValue &val) {
    return val.operator<<(out);
}

void displayList(MachineInstance *m) {
    std::stringstream ss;
    const char *delim = "";
    ss << "[";
    for (unsigned int i = 0; i < m->parameters.size(); ++i) {
        ss << delim << m->parameters[i].val;
        delim = ",";
    }
    ss << "]";
    m->setValue("DEBUG", Value(ss.str().c_str(), Value::t_string));
}

Value &PopListBackValue::operator()(MachineInstance *mi) {
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        std::stringstream ss;
        ss << mi->getName() << " no machine " << machine_list_name << " for LIST operation\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false;
        return last_result;
    }
    long i = machine_list->parameters.size() - 1;
    last_result = false;
    if (i >= 0) {
        last_result = machine_list->parameters[i].val;
        if (machine_list->parameters[i].machine && !last_result.cached_machine) {
            if (!machine_list->parameters[i].machine) {
                mi->lookup(machine_list->parameters[i]);
            }
            last_result.cached_machine = machine_list->parameters[i].machine;
        }
        if (remove_from_list) {
            machine_list->removeDependancy(machine_list->parameters[0].machine);
            machine_list->stopListening(machine_list->parameters[0].machine);
            machine_list->parameters.pop_back();
            machine_list->setNeedsCheck();
            displayList(machine_list);
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
std::ostream &PopListFrontValue::operator<<(std::ostream &out) const {
    if (remove_from_list) {
        return out << " TAKE FIRST FROM " << machine_list_name;
    }
    else {
        return out << " FIRST OF " << machine_list_name << " (" << last_result << ")";
    }
}
std::ostream &operator<<(std::ostream &out, const PopListFrontValue &val) {
    return val.operator<<(out);
}

Value &PopListFrontValue::operator()(MachineInstance *mi) {
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        std::stringstream ss;
        ss << mi->getName() << " no machine " << machine_list_name << " for LIST operation\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false;
        return last_result;
    }
    last_result = false;
    if (machine_list->_type == "REFERENCE") {
        if (machine_list->locals.size()) {
            last_result = machine_list->locals[0].val;
            if (machine_list->locals[0].machine && !last_result.cached_machine) {
                if (!machine_list->parameters[0].machine) {
                    mi->lookup(machine_list->parameters[0]);
                }
                last_result.cached_machine = machine_list->locals[0].machine;
            }
            if (remove_from_list) {
                machine_list->removeLocal(0);
                machine_list->setNeedsCheck();
                displayList(machine_list);
            }
        }
    }
    else {
        if (machine_list->parameters.size()) {
            last_result = machine_list->parameters[0].val;
            if (machine_list->parameters[0].machine && !last_result.cached_machine) {
                if (!machine_list->parameters[0].machine) {
                    mi->lookup(machine_list->parameters[0]);
                }
                last_result.cached_machine = machine_list->parameters[0].machine;
            }
            if (remove_from_list) {
                machine_list->removeDependancy(machine_list->parameters[0].machine);
                machine_list->stopListening(machine_list->parameters[0].machine);
                machine_list->parameters.erase(machine_list->parameters.begin());
                if (machine_list->_type == "LIST") {
                    machine_list->setNeedsCheck();
                    displayList(machine_list);
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
    index = other.index;
}
Value &ItemAtPosValue::operator()(MachineInstance *mi) {
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        std::stringstream ss;
        ss << mi->getName() << " no machine " << machine_list_name << " for LIST operation\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false;
        return last_result;
    }

#if 0
    if (last_process_time > machine_list->lastStateEvaluationTime()) {
        return last_result;
    }
#endif

    last_process_time = currentTime();
    if (machine_list->parameters.size()) {
        long idx = -1;
        if (index.kind == Value::t_symbol) {
            if (!mi->getValue(index.sValue).asInteger(idx)) {
                MessageLog::instance()->add("non-numeric index when evaluating ITEM AT pos");
                last_result = false;
                return last_result;
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
        if (idx >= 0 && idx < (long)machine_list->parameters.size()) {
            if (!machine_list->parameters[idx].machine) {
                mi->lookup(machine_list->parameters[idx]);
            }
            last_result = machine_list->parameters[idx].val;
            if (remove_from_list) {
                machine_list->parameters.erase(machine_list->parameters.begin() + idx);
                if (machine_list->_type == "LIST") {
                    machine_list->setNeedsCheck();
                    displayList(machine_list);
                }
            }
            return last_result;
        }
    }

    last_result = false;
    return last_result;
}

BitsetValue::BitsetValue(const BitsetValue &other) {
    machine_list_name = other.machine_list_name;
    machine_list = 0;
    state = other.state;
}
const Value &BitsetValue::operator()() {
    MachineInstance *mi = scope;
    machine_list = mi->lookup(machine_list_name);
    if (!machine_list) {
        std::stringstream ss;
        ss << mi->getName() << " no machine " << machine_list_name << " for LIST operation\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false;
        return last_result;
    }

#if 0
    if (last_process_time > machine_list->lastStateEvaluationTime()) {
        return last_result;
    }
#endif

    last_process_time = currentTime();
    unsigned long val = 0;
    for (unsigned int i = 0; i < machine_list->parameters.size(); ++i) {
        MachineInstance *entry = machine_list->parameters[i].machine;
        val *= 2;
        if (entry) {
            if (state == entry->getCurrentStateString()) {
                val += 1;
            }
        }
        else {
            bool x;
            if (machine_list->parameters[i].val.asBoolean(x)) {
                val += x ? 1 : 0;
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
const Value &EnabledValue::operator()() {
    MachineInstance *mi = scope;
    machine = mi->lookup(machine_name);
    if (!machine) {
        std::stringstream ss;
        ss << mi->getName() << " no machine " << machine_name << " for ENABLED test\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false;
        return last_result;
    }
    last_result = machine->enabled();
    return last_result;
}
std::ostream &EnabledValue::operator<<(std::ostream &out) const {
    return out << machine_name << " ENABLED? ";
}
std::ostream &operator<<(std::ostream &out, const EnabledValue &val) { return val.operator<<(out); }

DisabledValue::DisabledValue(const DisabledValue &other) {
    machine_name = other.machine_name;
    machine = 0;
}
DynamicValue *DisabledValue::clone() const { return new DisabledValue(*this); }
const Value &DisabledValue::operator()() {
    MachineInstance *mi = scope;
    machine = mi->lookup(machine_name);
    if (!machine) {
        std::stringstream ss;
        ss << mi->getName() << " no machine " << machine_name << " for DISABLED test\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false;
        return last_result;
    }
    last_result = !machine->enabled();
    return last_result;
}
std::ostream &DisabledValue::operator<<(std::ostream &out) const {
    return out << machine_name << " DISABLED? ";
}
std::ostream &operator<<(std::ostream &out, const DisabledValue &val) {
    return val.operator<<(out);
}

static int matched(const char *match, int index, void *user_data) {
    std::vector<std::string> *matches = static_cast<std::vector<std::string> *>(user_data);
    if (index == 0) {
        matches->push_back(match);
    }
    return 0;
}

const Value &PatternMatchValue::operator()() {
    MachineInstance *mi = scope;
    Value text = mi->getValue(property_name);
    if (text != SymbolTable::Null) {
        Value to_find = pattern->evaluate(mi);
        auto regex = create_pattern(to_find.asString().c_str());
        std::vector<std::string> matches;
        each_match(regex, text.asString().c_str(), 0, matched, &matches);
        if (!matches.empty()) {
            if (matches.size() == 1) {
                last_result = matches[0];
            }
            else {
                std::stringstream ss;
                std::string sep = "";
                for (auto i = matches.begin(); i != matches.end(); i++) {
                    ss << sep << *i;
                    if (sep.empty()) {
                        sep = separator.kind == Value::t_symbol ? mi->getValue(separator).asString()
                                                                : separator.asString();
                    }
                }
                last_result = Value(ss.str(), Value::t_string);
            }
            return last_result;
        }
    }
    last_result = "";
    return last_result;
}

PropertyLookupValue::PropertyLookupValue(const PropertyLookupValue &other) {
    machine_name = other.machine_name;
    property_name = other.property_name;
    machine = 0;
}
DynamicValue *PropertyLookupValue::clone() const { return new PropertyLookupValue(*this); }
const Value &PropertyLookupValue::operator()() {
    MachineInstance *mi = scope;
    Value prop = mi->getValue(property_name);
    if (prop.kind == Value::t_string) {
        prop.toSymbol();
    }
    if (prop.isNull()) {
        std::stringstream ss;
        ss << mi->getName() << " no property " << property_name << " for property lookup";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false;
        return last_result;
    }
    machine = mi->lookup(machine_name);
    if (!machine) {
        std::stringstream ss;
        ss << mi->getName() << " no machine " << machine_name << " for property lookup";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false;
        return last_result;
    }
    last_result = machine->getValue(prop.asString());
    if (last_result.isNull()) {
        last_result = prop; // machine couldn't resolve the property symbol; return the symbol
    }
    return last_result;
}
Value *PropertyLookupValue::getMutable(MachineInstance *current_scope) {
    machine = current_scope->lookup(machine_name);
    if (!machine) {
        LogMissingMachine(current_scope, machine_name, "for property lookup");
        last_result = false;
        return nullptr;
    }
    Value prop = current_scope->getValue(property_name);
    if (prop.kind == Value::t_symbol || prop.kind == Value::t_string) {
        return machine->getMutableValue(prop.sValue.c_str());
    }
    else {
        std::stringstream ss;
        ss << machine->getName() << " " << property_name << " does not resolve to a name";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = false;
        return nullptr;
    }
    return nullptr;
}
std::string PropertyLookupValue::resolvedName(MachineInstance *current_scope) {
    std::string result;
    machine = current_scope->lookup(machine_name);
    if (!machine) {
        std::stringstream ss;
        ss << current_scope->getName() << " no machine " << machine_name << " for property lookup";
        MessageLog::instance()->add(ss.str().c_str());
    }
    Value prop = current_scope->getValue(property_name);
    if (prop.kind == Value::t_symbol || prop.kind == Value::t_string) {
        result = machine_name;
        result += "." + prop.sValue;
    }
    else {
        std::stringstream ss;
        ss << machine->getName() << " " << property_name << " does not resolve to a name";
        MessageLog::instance()->add(ss.str().c_str());
    }
    return result;
}

std::ostream &PropertyLookupValue::operator<<(std::ostream &out) const {
    return out << property_name << " OF " << machine_name;
}
std::ostream &operator<<(std::ostream &out, const PropertyLookupValue &val) {
    return val.operator<<(out);
}

CastValue::CastValue(const CastValue &other) {
    property = other.property;
    kind = other.kind;
}
DynamicValue *CastValue::clone() const { return new CastValue(*this); }
Value &CastValue::operator()(MachineInstance *mi) {
    Value val = mi->properties.lookup(property.c_str());
    if (kind == "STRING") {
        last_result = val.asString();
    }
    else if (kind == "NUMBER") {
        long lValue = 0;
        if (val.asInteger(lValue)) {
            last_result = lValue;
        }
        else {
            last_result = false;
        }
        return last_result;
    }
    return last_result;
}
std::ostream &CastValue::operator<<(std::ostream &out) const {
    return out << "CAST(" << property << "," << kind << ") ";
}
std::ostream &operator<<(std::ostream &out, const CastValue &val) { return val.operator<<(out); }

ExistsValue::ExistsValue(const ExistsValue &other) {
    machine_name = other.machine_name;
    machine = 0;
}
DynamicValue *ExistsValue::clone() const { return new ExistsValue(*this); }
const Value &ExistsValue::operator()() {
    MachineInstance *mi = scope;
    machine = mi->lookup(machine_name);
    if (!machine) {
        last_result = false;
        return last_result;
    }
    last_result = true;
    return last_result;
}
std::ostream &ExistsValue::operator<<(std::ostream &out) const {
    return out << machine_name << " EXISTS ";
}
std::ostream &operator<<(std::ostream &out, const ExistsValue &val) { return val.operator<<(out); }

DynamicValue *ClassNameValue::clone() const { return new ClassNameValue(*this); }
const Value &ClassNameValue::operator()() {
    MachineInstance *mi = scope;
    machine = mi->lookup(machine_name);
    if (!machine || !machine->getStateMachine()) {
        std::stringstream ss;
        ss << mi->getName() << " no machine " << machine_name << " for CLASS test\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = "NULL";
        return last_result;
    }
    last_result = machine->getStateMachine()->name.c_str();
    return last_result;
}
std::ostream &ClassNameValue::operator<<(std::ostream &out) const {
    return out << "CLASS OF " << machine_name << " " << last_result;
}
std::ostream &operator<<(std::ostream &out, const ClassNameValue &val) {
    return val.operator<<(out);
}

DynamicValue *ChangingStateValue::clone() const { return new ChangingStateValue(*this); }
const Value &ChangingStateValue::operator()() {
    MachineInstance *mi = scope;
    machine = mi->lookup(machine_name);
    if (!machine || !machine->getStateMachine()) {
        std::stringstream ss;
        ss << mi->getName() << " no machine " << machine_name << " for CHANGING STATE test\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = "NULL";
        return last_result;
    }
    last_result = false;
    if (machine->active_actions.empty()) {
        return last_result;
    }
    std::list<Action *>::iterator iter = machine->active_actions.begin();
    while (iter != machine->active_actions.end()) {
        Action *a = *iter++;
        MoveStateAction *msa = dynamic_cast<MoveStateAction *>(a);
        if (msa && msa->value == machine->getCurrent()) {
            last_result = true;
            return last_result;
        }
    }
    return last_result;
}

std::ostream &ChangingStateValue::operator<<(std::ostream &out) const {
    return out << machine_name << " CHANGING STATE (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const ChangingStateValue &val) {
    return val.operator<<(out);
}

AsFormattedStringValue::AsFormattedStringValue(const AsFormattedStringValue &other) {
    property_name = other.property_name;
    format = other.format;
}
DynamicValue *AsFormattedStringValue::clone() const { return new AsFormattedStringValue(*this); }
const Value &AsFormattedStringValue::operator()() {
    MachineInstance *mi = scope;
    const Value &v = mi->getValue(property_name);
    if (v == SymbolTable::Null) {
        std::stringstream ss;
        ss << mi->getName() << " cannot find  " << property_name << " for AS STRING\n";
        MessageLog::instance()->add(ss.str().c_str());
        last_result = 0;
        return last_result;
    }
    last_result = Value(v.asString(format.c_str()), Value::t_string);
    return last_result;
}
std::ostream &AsFormattedStringValue::operator<<(std::ostream &out) const {
    return out << "AS STRING WITH FORMAT \"" << format << "\" (" << last_result << ")";
}
std::ostream &operator<<(std::ostream &out, const AsFormattedStringValue &val) {
    return val.operator<<(out);
}
