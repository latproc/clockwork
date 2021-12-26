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

#include <lib_clockwork_client/includes.hpp>
// // #include "DebugExtra.h"
// // #include "Logger.h"
// #include "IOComponent.h"
#include "MachineInstance.h"
// // #include "MessageLog.h"
#include "CallMethodAction.h"

// -- CallMethodAction - send a command message to a machine and wait for a response
//    the remote machine must send a reply or this action hangs forever

CallMethodActionTemplate::CallMethodActionTemplate(CStringHolder msg,
                                                   CStringHolder dest,
                                                   CStringHolder timeout,
                                                   CStringHolder error)
    : message(msg), target(dest), timeout_symbol(timeout), error_symbol(error) {
}

Action *CallMethodActionTemplate::factory(MachineInstance *mi) {
    return new CallMethodAction(mi, *this);
}

std::ostream &CallMethodActionTemplate::operator<<(std::ostream &out) const {
    return out << "CallMethodActionTemplate " << message.get() << " on "
               << target.get();
}

CallMethodAction::CallMethodAction(MachineInstance *mi,
                                   CallMethodActionTemplate &eat)
    : Action(mi), message(eat.message), target(eat.target), target_machine(0) {
    if (eat.timeout_symbol.get()) {
        timeout_msg = new CStringHolder(eat.timeout_symbol.get());
    }
    if (eat.error_symbol.get()) {
        error_msg = new CStringHolder(eat.error_symbol.get());
    }
}

Action::Status CallMethodAction::run() {
    owner->start(this);
    target_machine = owner->lookup(target.get());
    if (!target_machine) {
        char buf[150];
        snprintf(buf, 100, "%s CallMethodAction failed to find machine %s",
                 owner->getName().c_str(), target.get());
        MessageLog::instance()->add(buf);
        error_str = strdup(buf);
        return Failed;
    }
    if (target_machine == owner) {
        DBG_M_MESSAGING << owner->getName() << " sending message "
                        << message.get() << "\n";
        std::string short_name(message.get());
        if (short_name.rfind('.') != std::string::npos) {
            short_name.substr(short_name.rfind('.'));
            Message msg(short_name.c_str());
            status = owner->execute(msg, target_machine);
        } else {
            status = owner->execute(Message(message.get()), target_machine);
        }
        //status = Action::Complete;
        if (status == Action::Complete) {
            owner->stop(this);
        }
        return status;
    } else if (target_machine->enabled()) {
        if (!getTrigger() || getTrigger()->fired() || !trigger->enabled()) {
            setTrigger(owner->setupTrigger(target_machine->getName(),
                                           message.get(), "_done"));
            owner->sendMessageToReceiver(new Message(message.get()),
                                         target_machine, true);
            status = Action::Running;
        } else {
            status = Running;
            std::stringstream ss;
            ss << "NOTICE: " << *this << " calling " << message.get() << " on "
               << target_machine->getName()
               << " already has trigger: " << trigger->getName()
               << " that has not fired";
            MessageLog::instance()->add(ss.str().c_str());
            DBG_MSG << ss.str() << "\n";
            /*  trigger->disable();
                setTrigger(owner->setupTrigger(target_machine->getName(), message.get(), "_done"));
                owner->sendMessageToReceiver(new Message(message.get()), target_machine, true);
            */
        }
    } else {
        char buf[100];
        snprintf(buf, 100, "Call to disabled machine: %s",
                 target_machine->getName().c_str());
        MessageLog::instance()->add(buf);
        status = Action::Failed;
        error_str = strdup(buf);
        abort();
        owner->stop(this);
    }
    return status;
}

Action::Status CallMethodAction::checkComplete() {
    if (status == Complete || status == Failed) {
        return status;
    }
    if (status == Action::New) {
        if (run() == Action::New) {
            return status;
        }
    }
    // If the action is complete it will have cleared the trigger by now.
    // the following test treats the Call as complete if there is no trigger
    if (status == New) {
        if (!trigger || trigger->fired()) {
            setTrigger(owner->setupTrigger(target_machine->getName(),
                                           message.get(), "_done"));
            owner->sendMessageToReceiver(new Message(message.get()),
                                         target_machine, true);
            status = Action::Running;
        }
        return status;
    }
    if (!trigger || trigger->fired()) {
        status = Action::Complete;
        owner->stop(this);
        return status;
    }
    assert(status == Action::Running);
    return status;
}

std::ostream &CallMethodAction::operator<<(std::ostream &out) const {
    out << "CallMethodAction " << message.get() << " on " << target.get();
    if (trigger) {
        if (!trigger->enabled()) {
            out << " (trigger disabled) ";
        } else {
            if (!trigger->fired()) {
                out << " (Waiting for trigger to fire) ";
            } else {
                out << " (trigger fired) ";
            }
        }
    } else {
        out << " (no trigger set) ";
    }
    return out;
}
