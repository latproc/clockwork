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

#include "WaitAction.h"
#include "DebugExtra.h"
#include "FireTriggerAction.h"
#include "IOComponent.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "Scheduler.h"

WaitActionTemplate::WaitActionTemplate(CStringHolder property)
    : wait_time(-1), property_name(property.get()) {}

WaitActionTemplate::WaitActionTemplate(Value &val) : wait_time(-1), property_name("") {
    if (val.kind == Value::t_integer) {
        wait_time = val.iValue;
    }
    else {
        property_name = val.sValue;
    }
}

WaitActionTemplate::WaitActionTemplate(long delta) : wait_time(delta), property_name("") {}

std::ostream &WaitActionTemplate::operator<<(std::ostream &out) const { return out << wait_time; }

void WaitActionTemplate::toC(std::ostream &out, std::ostream &vars) const {
    vars << "\tunsigned long wait_start;\n";
    out << "\tctx->wait_start = m->machine.TIMER;\n"
        << "\twhile (m->machine.TIMER - ctx->wait_start < " << wait_time << ") {\n"
        << "\t  struct RTScheduler *scheduler = RTScheduler_get();\n"
        << "\t  while (!scheduler) {\n"
        << "\t    taskYIELD();\n"
        << "\t    scheduler = RTScheduler_get();\n"
        << "\t  }\n"
        << "\t  goto_sleep(&m->machine);\n"
        << "\t  RTScheduler_add(scheduler, ScheduleItem_create(" << wait_time
        << " - (m->machine.TIMER - ctx->wait_start), &m->machine));\n"
        << "\t  RTScheduler_release();\n"
        << "\t  ccrReturn(0);\n"
        << "\t}\n";
}

WaitAction::WaitAction(MachineInstance *mi, WaitActionTemplate &wat)
    : Action(mi), wait_time(wat.wait_time), property_name(wat.property_name), use_property(false) {
    if (wait_time == -1) {
        use_property = true;
    }
}

WaitAction::~WaitAction() {}

Action *WaitActionTemplate::factory(MachineInstance *mi) { return new WaitAction(mi, *this); }

Action *WaitForActionTemplate::factory(MachineInstance *mi) { return new WaitForAction(mi, *this); }

Action::Status WaitAction::run() {

    Value v;
    owner->start(this);
    if (wait_time == -1) {
        // lookup property
        if (property_name.find('.') != std::string::npos) {
            v = owner->getValue(property_name);
            DBG_M_PROPERTIES << "looking up property " << property_name << " "
                             << ": " << v << "\n";
        }
        else {
            v = owner->getValue(property_name.c_str());
        }
        if (v.kind == Value::t_float) {
            DBG_M_PROPERTIES << "Error: expected an integer value for wait_time, got: " << v
                             << "\n";
            wait_time = v.trunc();
        }
        else if (v.kind != Value::t_integer) {
            wait_time = 0;
            DBG_M_PROPERTIES << "Error: expected an integer value for wait_time, got: " << v
                             << "\n";
        }
        else {
            wait_time = v.iValue;
        }
    }
    if (wait_time == 0) {
        if (trigger && !trigger->fired() && trigger->enabled()) {
            trigger->fire();
        }
        status = Complete;
        owner->stop(this);
    }
    else {
        status = Running;
        if (trigger) {
            DBG_MESSAGING << owner->getName() << " " << *this << " removing old trigger\n";
            cleanupTrigger();
        }
        char buf[100];
        snprintf(buf, 100, "%s WaitTimer", owner->getName().c_str());
        trigger = new Trigger(buf);
        trigger->addHolder(this);
        FireTriggerAction *fta = new FireTriggerAction(owner, trigger);
        Scheduler::instance()->add(new ScheduledItem(wait_time * 1000, fta));
        assert(!trigger->fired());
        if (use_property) {
            wait_time = -1; // next time, find the property value again
        }
    }
    DBG_M_PROPERTIES << "waiting " << wait_time << "\n";
    return status;
}

Action::Status WaitAction::checkComplete() {
    DBG_M_MESSAGING << owner->getName() << " " << *this << " checking if complete\n";
    if (!trigger) {
        DBG_M_MESSAGING << owner->getName() << " " << *this << " has no trigger\n";
        status = Complete;
        owner->stop(this);
    }
    else if (trigger && trigger->fired()) {
        DBG_M_MESSAGING << owner->getName() << " " << *this << " trigger has fired\n";
        status = Complete;
        owner->stop(this);
    }
    else if (trigger) {
        DBG_M_MESSAGING << owner->getName() << " " << *this << " trigger has not fired\n";
        trigger->report("not fired");
    }
    return status;
}

std::ostream &WaitAction::operator<<(std::ostream &out) const {
    if (wait_time == -1) {
        return out << "WAIT " << property_name;
    }
    else {
        return out << "WAIT " << wait_time;
    }
}

WaitForActionTemplate::WaitForActionTemplate(CStringHolder targ, Value value)
    : target(targ), value(value) {}

Action::Status WaitForAction::run() {
    owner->start(this);
    machine = owner->lookup(target.get());
    status = Running;
    return checkComplete();
}

Action::Status WaitForAction::checkComplete() {
    if (status == Complete || status == Failed) {
        return status;
    }
    const Value &target_prop = owner->getValue(target.get());
    if (!target_prop.isNull()) {
        Value source = value.kind == Value::t_symbol ? owner->getValue(value) : value;
        bool equal =
            value.kind == Value::t_float ? source.identical(target_prop) : source == target_prop;
        if (equal) {
            status = Complete;
            owner->stop(this);
        }
    }
    else if (machine && value.kind == Value::t_symbol) {
        if (machine->getCurrent().getName() == value.asString()) {
            status = Complete;
            owner->stop(this);
        }
    }
    else if (machine && (value.kind == Value::t_string || value.kind == Value::t_integer ||
                         value.kind == Value::t_float)) {
        const Value &val = machine->getValue("VALUE");
        bool equal = value.kind == Value::t_float ? value.identical(val) : value == val;
        if (equal) {
            status = Complete;
            owner->stop(this);
        }
    }
    else if (!machine) {
        const Value &val = owner->getValue(target.get());
        const Value &test = value.kind == Value::t_symbol ? owner->getValue(value) : value;
        if (val == test) {
            status = Complete;
            owner->stop(this);
        }
    }
    else {
        std::stringstream ss;
        ss << owner->getName() << " unexpected parameters in " << *this << "\n";
        error_str = strdup(ss.str().c_str());
        DBG_ACTIONS << *this << " failed: " << ss.str();
        status = Failed;
        owner->stop(this);
    }
    return status;
}

std::ostream &WaitForAction::operator<<(std::ostream &out) const {
    return out << "WaitForAction " << target << " IS " << value;
}
