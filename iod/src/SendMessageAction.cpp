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

#include "SendMessageAction.h"
#include "Channel.h"
#include "DebugExtra.h"
#include "Dispatcher.h"
#include "IOComponent.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"

SendMessageActionTemplate::SendMessageActionTemplate(Value msg, Value dest)
    : message(msg), target(dest), target_machine(0) {}

SendMessageActionTemplate::SendMessageActionTemplate(Value msg, MachineInstance *dest)
    : message(msg), target(dest->fullName()), target_machine(dest) {}

Action *SendMessageActionTemplate::factory(MachineInstance *mi) {
    return new SendMessageAction(mi, *this);
}

std::ostream &SendMessageActionTemplate::operator<<(std::ostream &out) const {
    return out << "SendMessageActionTemplate " << message << " " << ((target != 0) ? target : "");
}

void SendMessageActionTemplate::toC(std::ostream &out, std::ostream &vars) const {
    ExportState::add_message(message.asString());

    out << "\tcw_send(";
    if (target.asString() != "") {
        out << "m->_" << target << ", ";
    }
    else {
        out << "0, ";
    }
    out << "&m->machine, cw_message_" << message << ");\n";
}

SendMessageAction::SendMessageAction(MachineInstance *mi, SendMessageActionTemplate &eat)
    : Action(mi), message(eat.message), target(eat.target), target_machine(eat.target_machine) {}

// LPC SEND must run dest work in this pass. Clock notify does not use
// this path — it only enqueues.
//
// dest->idle() first drains mail into a HandleMessageAction (status New)
// and returns without running it. Treating any executingCommand() as
// "stop" left that New handler queued; a later processAll might run it,
// or a MOVE off the source LIST could leave it stranded. Finish New /
// NeedsRetry work here.
//
// Do not idle a dest that already has a Running/Suspended command.
// Unconditional idle() nested into dest while a command such as
// SEND-to-SELF was still Running and left that dest pinned.
static bool destHasDeliverableWork(MachineInstance *dest) {
    if (dest->hasMail()) {
        return true;
    }
    Action *a = dest->executingCommand();
    if (!a) {
        return false;
    }
    Action::Status s = a->getStatus();
    return s == Action::New || s == Action::NeedsRetry;
}

static void idleLocalSendDest(MachineInstance *dest) {
    if (!dest) {
        return;
    }
    Action *pre = dest->executingCommand();
    if (pre) {
        Action::Status s = pre->getStatus();
        if (s != Action::New && s != Action::NeedsRetry) {
            dest->idleReadyDependents();
            return;
        }
    }
    for (int n = 0; n < 16; ++n) {
        if (!destHasDeliverableWork(dest)) {
            // Dest COMMAND that SET a listened machine bumps needs_check
            // while actions still run; setNeedsCheck may skip pending_state.
            // WHEN on that property (no SET SELF) must run after the command.
            dest->idleReadyWork();
            dest->idleReadyDependents();
            return;
        }
        dest->idle();
        Action *a = dest->executingCommand();
        if (a) {
            Action::Status s = a->getStatus();
            if (s != Action::New && s != Action::NeedsRetry) {
                dest->idleReadyDependents();
                return;
            }
        }
    }
    dest->idleReadyWork();
    dest->idleReadyDependents();
}

Action::Status SendMessageAction::run() {
    owner->start(this);
    if (target != 0) {
        target.cached_machine = 0; // clear cached value
        target_machine = owner->lookup(target);
        DBG_ACTIONS << *this << "\n";
        if (!target_machine) {
            // no target with the given name, however in the case of channels,
            // the target may be an active channel of the given type
            Channel *chn = Channel::findByType(target.asString());
            target_machine = chn;
        }
        if (target_machine) {
            std::string msg_str;
            if (message.kind == Value::t_symbol) {
                Value msg_val = owner->getValue(message);
                if (msg_val != SymbolTable::Null) {
                    msg_str = msg_val.asString();
                }
                else {
                    msg_str = message.asString();
                }
            }
            else {
                msg_str = message.asString();
            }
            owner->sendMessageToReceiver(msg_str.c_str(), target_machine);
            idleLocalSendDest(target_machine);
            if (target_machine->_type == "LIST" && target_machine->enabled()) {
                for (unsigned int i = 0; i < target_machine->parameters.size(); ++i) {
                    MachineInstance *entry = target_machine->parameters[i].machine;
                    if (entry) {
                        owner->sendMessageToReceiver(msg_str.c_str(), entry);
                        idleLocalSendDest(entry);
                    }
                }
            }
            else if (target_machine->_type == "REFERENCE" && target_machine->enabled() &&
                     target_machine->locals.size()) {
                for (unsigned int i = 0; i < target_machine->locals.size(); ++i) {
                    MachineInstance *entry = target_machine->locals[i].machine;
                    if (entry) {
                        owner->sendMessageToReceiver(msg_str.c_str(), entry);
                        idleLocalSendDest(entry);
                    }
                }
            }
            status = Action::Complete;
        }
        else {
            std::stringstream ss;
            ss << *this << " Error: cannot find target machine " << target;
            MessageLog::instance()->add(ss.str().c_str());
            NB_MSG << ss.str() << "\n";
            status = Action::Failed;
        }
    }
    else {
        Message m(message.asString().c_str());
        Package *p = new Package(owner, 0, m);
        Dispatcher::instance()->deliver(p);
    }
    owner->stop(this);
    return status;
}

Action::Status SendMessageAction::checkComplete() { return Action::Complete; }

std::ostream &SendMessageAction::operator<<(std::ostream &out) const {
    if (!target_machine) {
        return out << owner->getName() << ": SendMessageAction " << message
                   << " TO unknown target: " << target.asString() << "\n";
    }
    else {
        return out << owner->getName() << ": SendMessageAction " << message << " TO " << target
                   << "\n";
    }
}
