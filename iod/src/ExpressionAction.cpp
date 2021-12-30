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

#include "ExpressionAction.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"

// attempt to find the value of the property named in v.    v may be the name
//  of a machine, in which case we need to look for the property named
//  in the VALUE property of that machine
static const Value &resolve(MachineInstance *scope, const Value &v) {
    if (v == SymbolTable::Null) {
        return v;
    }
    if (v.kind != Value::t_symbol && v.kind != Value::t_string) {
        return v;
    }
    // start by checking if the symbol refers to a machine, in which case
    // we need the VALUE property of the machine
    const Value &local_v = scope->getValue(v.sValue);
    auto machine = scope->lookup(v.sValue);
    const Value &target_v =
        (local_v != SymbolTable::Null) ? local_v : ((machine) ? machine->getValue("VALUE") : v);
    // the machine doesn't have a VALUE? v must be refering to a local property
    if (target_v == SymbolTable::Null) {
        return scope->getValue(v.sValue);
    }
    // strictly speaking target_t is required to be a symbol here, or at least a string..
    if (target_v.kind != Value::t_symbol && target_v.kind != Value::t_string) {
        char buf[400];
        snprintf(buf, 400,
                 "%s: SET x TO PROPERTY... expected a symbol for a property name but found type %d",
                 scope->getName().c_str(), (int)target_v.kind);
        MessageLog::instance()->add(buf);
        DBG_MSG << buf << "\n";
    }
    return target_v;
}

Action::Status ExpressionAction::run() {
    owner->start(this);
    status = Running;
    const Value &v = owner->getValue(lhs.get());
    if (v != SymbolTable::Null) {
        switch (op) {
        case ExpressionActionTemplate::opInc:
        case ExpressionActionTemplate::opDec:
            if (v.kind != Value::t_integer || rhs.kind != Value::t_integer) {
                status = Failed;
            }
            else {
                owner->setValue(lhs.get(), v.iValue + rhs.iValue);
            }
            break;
        case ExpressionActionTemplate::opSet:
            if (rhs.kind == Value::t_symbol) {
                const Value &lookup_v = resolve(owner, rhs);
                MachineInstance *scope = owner;
                if (extra != SymbolTable::Null) {
                    auto machine = owner->lookup(extra.asString());
                    if (machine) {
                        scope = machine;
                    }
                    else {
                        // error no machine for the property source
                        char buf[400];
                        snprintf(buf, 400,
                                 "%s: no machine can be found for SET %s TO PROPERTY %s OF %s",
                                 owner->getName().c_str(), lhs.get(), rhs.asString().c_str(),
                                 extra.asString().c_str());
                        MessageLog::instance()->add(buf);
                        DBG_MSG << buf << "\n";
                        status = Failed;
                        goto finished_run;
                    }
                }
                const Value &property_v = resolve(scope, lookup_v);
                if (property_v == SymbolTable::Null) {
                    // error no property found using the property name as the value
                    DBG_MSG << owner->getName() << "could not find property " << lookup_v << " for "
                            << *this << "\n";
                    owner->setValue(lhs.get(), Value(rhs.asString(), Value::t_string));
                }
                else {
                    DBG_MSG << owner->getName() << " dereferenced property " << lookup_v
                            << " and found " << property_v << "\n";
                    owner->setValue(lhs.get(), property_v);
                }
            }
            else {
                if (extra != SymbolTable::Null) {
                    // error, need a property name to lookup a propert of another machine
                    char buf[400];
                    snprintf(buf, 400, "%s: no property name given for SET %s TO PROPERTY %s OF %s",
                             owner->getName().c_str(), lhs.get(), rhs.asString().c_str(),
                             extra.asString().c_str());
                    MessageLog::instance()->add(buf);
                    DBG_MSG << buf << "\n";
                }
                else {
                    owner->setValue(lhs.get(), rhs);
                }
            }
            status = Complete;
            break;
        }
        if (status != Failed) {
            owner->setValue(lhs.get(), v);
            status = Complete;
        }
    }
    else {
        owner->setValue(lhs.get(), rhs);
        status = Complete;
    }
finished_run:
    owner->setNeedsCheck();
    owner->stop(this);
    return status;
}

Action::Status ExpressionAction::checkComplete() { return Complete; }

std::ostream &ExpressionAction::operator<<(std::ostream &out) const {
    if (extra == SymbolTable::Null) {
        return out << "ExpressionAction " << lhs.get() << " " << op << " " << rhs << "\n";
    }
    else {
        return out << "ExpressionAction " << lhs.get() << " " << op << " " << rhs << " machine "
                   << extra << "\n";
    }
}

Action *ExpressionActionTemplate::factory(MachineInstance *mi) {
    return new ExpressionAction(mi, *this);
}
