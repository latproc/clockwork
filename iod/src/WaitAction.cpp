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

WaitAction::WaitAction(MachineInstance *mi, WaitActionTemplate &wat)
    : Action(mi), wait_time(wat.wait_time), property_name(wat.property_name), use_property(false) {
    if (wait_time == -1) {
        use_property = true;
    }
}

WaitAction::~WaitAction() { NB_MSG << "Destructing " << *this; }

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
    gettimeofday(&start_time, 0);
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
    /*
        struct timeval now;
        gettimeofday(&now, 0);
        long delta = get_diff_in_microsecs(&now, &start_time);
        if (delta >= wait_time * 1000) { status = Complete; owner->stop(this); }
        //    else DBG_M_ACTIONS << "still waiting " << (wait_time*1000-delta) << "\n";
    */
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
    : target(targ), value(value) {
    std::cout << "Waitfor temlplate: " << *this << "\n";
}

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
    if (machine && value.kind == Value::t_symbol) {
        if (machine->getCurrent().getName() == value.asString()) {
            status = Complete;
            owner->stop(this);
        }
        else {
            status = Running;
        }
    }
    else if (!machine) {
        const Value &val = owner->getValue(target.get());
        const Value &test = value.kind == Value::t_symbol ? owner->getValue(value) : value;
        if (val == test) {
            status = Complete;
            owner->stop(this);
        }
        else {
            status = Running;
        }
    }
    else {
        std::stringstream ss;
        ss << owner->getName() << " unexpected parameters in " << *this;
        error_str = strdup(ss.str().c_str());
        status = Failed;
        owner->stop(this);
    }
    return status;
}

std::ostream &WaitForAction::operator<<(std::ostream &out) const {
    return out << "WaitForAction " << target << " IS " << value;
}
