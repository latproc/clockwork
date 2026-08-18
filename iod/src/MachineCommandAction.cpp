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

#include "MachineCommandAction.h"
#include "AbortAction.h"
#include "DebugExtra.h"
#include "ExpressionAction.h"
#include "ExpressionPcode.h"
#include "IfCommandAction.h"
#include "IOComponent.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "SendMessageAction.h"
#include <algorithm>
#include <sstream>

namespace {

std::string predicateText(const Predicate *p) {
    if (!p) {
        return "";
    }
    std::ostringstream oss;
    oss << *p;
    return oss.str();
}

std::vector<std::string> sortedCopy(const std::vector<std::string> &in) {
    std::vector<std::string> out(in);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

MachineCommandTemplate::MachineCommandTemplate(CStringHolder cmd_name, CStringHolder state,
                                               bool auto_switch)
    : command_name(cmd_name), state_name(state), guard(0), timeout(0), switch_state(auto_switch) {
    if (!auto_switch && state.get() && *state.get()) {
        within_states.push_back(state.get());
    }
}

MachineCommandTemplate::~MachineCommandTemplate() { delete guard; }

void MachineCommandTemplate::setWithinStates(std::vector<std::string> states) {
    within_states = std::move(states);
}

void MachineCommandTemplate::setGuard(Predicate *p) {
    delete guard;
    guard = p ? new Condition(p) : 0;
}

bool MachineCommandTemplate::matchesWithin(const std::string &state) const {
    if (switch_state) {
        return true;
    }
    if (within_states.empty()) {
        return true;
    }
    return std::find(within_states.begin(), within_states.end(), state) != within_states.end();
}

bool MachineCommandTemplate::sameWithinSet(const std::vector<std::string> &other) const {
    return sortedCopy(within_states) == sortedCopy(other);
}

bool MachineCommandTemplate::sameGuard(const Predicate *other) const {
    const Predicate *mine = (guard && guard->predicate) ? guard->predicate : 0;
    return predicateText(mine) == predicateText(other);
}

bool MachineCommandTemplate::isDuplicateOf(const std::vector<std::string> &within,
                                           const Predicate *g) const {
    if (switch_state) {
        return false;
    }
    return sameWithinSet(within) && sameGuard(g);
}

void MachineCommandTemplate::describeGuards(std::ostream &out) const {
    if (switch_state) {
        out << "AUTO SWITCH to " << state_name.get();
        return;
    }
    bool wrote = false;
    if (!within_states.empty()) {
        out << "WITHIN ";
        for (size_t i = 0; i < within_states.size(); ++i) {
            if (i) {
                out << ", ";
            }
            out << within_states[i];
        }
        wrote = true;
    }
    if (guard && guard->predicate) {
        if (wrote) {
            out << " ";
        }
        out << "WHEN " << *guard->predicate;
        wrote = true;
    }
    if (!wrote) {
        out << "unrestricted";
    }
}

void MachineCommandTemplate::setActionTemplates(std::list<ActionTemplate *> &new_actions) {
    BOOST_FOREACH (ActionTemplate *at, new_actions) {
        action_templates.push_back(at);
    }
}
void MachineCommandTemplate::setActionTemplate(ActionTemplate *at) {
    action_templates.push_back(at);
}

MachineCommand::MachineCommand(MachineInstance *mi, MachineCommandTemplate *mct)
    : Action(mi), last_step(0), current_step(0), command_name(mct->command_name),
      state_name(mct->getStateName()), within_states(mct->getWithinStates()),
      guard(mct->getGuard() ? new Condition(*mct->getGuard()) : 0), timeout_trigger(0),
      switch_state(mct->switch_state), fast_tried(false), fast_ok(false) {
    BOOST_FOREACH (ActionTemplate *t, mct->action_templates) {
        //DBG_M_ACTIONS << "copying action " << (*t) << " for machine " << mi->_name << "\n";
        actions.push_back(t->factory(mi));
        /*
                // A THROW is implemented as a SendMessage with no destination, followed by an abort
                // we insert the abort here if necessary.
                SendMessageAction *sma = dynamic_cast<SendMessageAction*>(t);
                if (sma && sma->target == SymbolTable::Null) {
                    AbortActionTemplate aa;
                    actions.push_back(aa.factory(mi));
                }
        */
    }
}

MachineCommand::~MachineCommand() {
    BOOST_FOREACH (Action *a, actions) {
        owner->active_actions.remove(a);
        if (a->getTrigger() && a->getTrigger()->enabled() && !a->getTrigger()->fired()) {
            a->disableTrigger();
        }
        a->release();
    }
    if (timeout_trigger) {
        timeout_trigger->disable();
        timeout_trigger = timeout_trigger->release();
    }
    delete guard;
}

void MachineCommand::addAction(Action *a, ActionParameterList *params) { actions.push_back(a); }

void MachineCommand::setActions(std::list<Action *> &new_actions) {
    std::copy(new_actions.begin(), new_actions.end(), back_inserter(actions));
}

std::ostream &MachineCommand::operator<<(std::ostream &out) const {
    out << "Command " << owner->getName() << "." << command_name << " (at step " << current_step
        << ")";
    if (switch_state && state_name.get() && *state_name.get()) {
        out << " AUTO SWITCH to " << state_name.get();
    }
    else if (!within_states.empty()) {
        out << " WITHIN ";
        for (size_t i = 0; i < within_states.size(); ++i) {
            if (i) {
                out << ", ";
            }
            out << within_states[i];
        }
    }
    if (guard && guard->predicate) {
        out << " WHEN " << *guard->predicate;
    }
    return out;
}

bool MachineCommand::matchesWithin(const std::string &state) const {
    if (switch_state) {
        return true;
    }
    if (within_states.empty()) {
        return true;
    }
    return std::find(within_states.begin(), within_states.end(), state) != within_states.end();
}

bool MachineCommand::matches(MachineInstance *mi) const {
    if (!mi) {
        return false;
    }
    if (!matchesWithin(mi->getCurrent().getName())) {
        return false;
    }
    if (guard && guard->predicate) {
        return (*guard)(mi);
    }
    return true;
}

Action::Status MachineCommand::checkAction(Action *a, Action::Status stat) { return stat; }

bool MachineCommand::canRunFast() const {
    for (size_t i = 0; i < actions.size(); ++i) {
        Action *a = actions[i];
        if (dynamic_cast<ExpressionAction *>(a)) {
            ExpressionAction *ea = static_cast<ExpressionAction *>(a);
            if (!ea->expr) {
                return false;
            }
            continue;
        }
        if (IfCommandAction *ia = dynamic_cast<IfCommandAction *>(a)) {
            if (!ia->command || !ia->command->canRunFast()) {
                return false;
            }
            continue;
        }
        if (IfElseCommandAction *ie = dynamic_cast<IfElseCommandAction *>(a)) {
            if (!ie->command || !ie->command->canRunFast()) {
                return false;
            }
            if (ie->else_command && !ie->else_command->canRunFast()) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

bool MachineCommand::runFastBody() {
    for (size_t i = 0; i < actions.size(); ++i) {
        Action *a = actions[i];
        if (ExpressionAction *ea = dynamic_cast<ExpressionAction *>(a)) {
            if (!ea->applyAssign()) {
                return false;
            }
            continue;
        }
        if (IfCommandAction *ia = dynamic_cast<IfCommandAction *>(a)) {
            if (ia->condition(owner)) {
                if (!ia->command->runFastBody()) {
                    return false;
                }
            }
            continue;
        }
        if (IfElseCommandAction *ie = dynamic_cast<IfElseCommandAction *>(a)) {
            if (ie->condition(owner)) {
                if (!ie->command->runFastBody()) {
                    return false;
                }
            }
            else if (ie->else_command) {
                if (!ie->else_command->runFastBody()) {
                    return false;
                }
            }
            continue;
        }
        return false;
    }
    return true;
}

Action::Status MachineCommand::runActions() {
    while (current_step < actions.size()) {
        Action *a = actions[current_step]->retain();
        setBlocker(a);
        suspend();
        DBG_M_ACTIONS << owner->getName() << " about to execute " << *a << "\n";
        Action::Status stat = (*a)();

        // An abort action needs special processing to cause the action pointer
        // to immediately move to the end of the action list
        auto aa = dynamic_cast<AbortAction *>(a);
        if (aa) {
            std::stringstream ss;
            ss << "ABORTING: " << *this << " at action " << *a << "\n";
            abort();
            char *err_msg = strdup(ss.str().c_str());
            MessageLog::instance()->add(err_msg);
            error_str = err_msg;
            if (stat == Failed) {
                error_str = a->error();
            }
            owner->stop(a);
            last_step = current_step;      // remember the command that aborted
            current_step = actions.size(); // jump to the end of the action list
            setBlocker(0);
            Action *x = owner->executingCommand();
            if (x == a) {
                std::stringstream ss;
                ss << owner->getName() << " " << (*this) << " action '" << *a;
                MessageLog::instance()->add(ss.str().c_str());
                owner->stop(a);
            }
            a->release();
            return Complete;
        }
        if (stat == Action::Failed) {
            std::stringstream ss;
            ss << " action: " << *a << " running on " << owner->fullName();
            if (a->aborted()) {
                ss << " aborted (" << a->error() << ")";
            }
            else {
                ss << " failed to start (" << a->error() << ")";
            }
            char *err_msg = strdup(ss.str().c_str());
            MessageLog::instance()->add(err_msg);
            error_str = err_msg;
        }
        if (stat == Action::NeedsRetry || stat == Action::New) {
            std::stringstream ss;
            ss << " action: " << *a << " failed temporarily (" << a->error() << ")\n";
            MessageLog::instance()->add(ss.str().c_str());
            NB_MSG << ss.str() << "\n";
            owner->stop(a);
            a->release();
            status =
                Running; // the current action needs a restart so overall this command list is still running
            return status;
        }
        if (!a->complete()) {
            DBG_M_ACTIONS << "leaving action: " << *a << " to run for a while\n";
            a->release();
            return stat; // currently running at curent_step
        }
        Action *x = owner->executingCommand();
        if (x == a) {
            std::stringstream ss;
            ss << owner->getName() << " " << (*this) << " action '" << *a
               << "' didn't remove itself, stopping it manually.";
            MessageLog::instance()->add(ss.str().c_str());
            DBG_M_ACTIONS << ss.str() << "\n";
            owner->stop(a);
        }
        setBlocker(0);
        last_step = current_step++;
        DBG_M_ACTIONS << owner->getName() << " completed action: " << *a << "\n";
        a->release();
    }
    DBG_M_ACTIONS << owner->getName() << " " << *this << " completed all actions\n";
    return Complete;
}

Action::Status MachineCommand::run() {
    owner->start(this);
    status = Running;
    last_step = 0;
    current_step = 0;
    if (!switch_state && !matches(owner)) {
        /*
            std::stringstream ss;
            ss << "Command " << (*this) << " was ignored due to a mismatch of current state (" << owner->getCurrent().getName()
            << ") and state required by the command (" << state_name << ")";
            char *err_msg = strdup(ss.str().c_str());
            MessageLog::instance()->add(err_msg);
            DBG_M_ACTIONS << err_msg << "\n";
            result_str = err_msg;
        */
        status = Complete;
        owner->stop(this);

        return status; // no steps to run
    }
    if (current_step == actions.size()) {
        result_str = "command finished: nothing to do\n";
        //DBG_M_ACTIONS << command_name.get() << " " << result_str.get() << "\n";
        status = Complete;
        owner->stop(this);
        return status; // no steps to run
    }

    const bool expr_fast = ExpressionPcode::fastPathEnabled();
    if (expr_fast) {
        owner->beginDeferredPropertyNotify();
        if (!fast_tried) {
            fast_ok = canRunFast();
            fast_tried = true;
        }
    }
    Action::Status stat = Complete;
    if (expr_fast && fast_ok) {
        if (!runFastBody()) {
            stat = Failed;
        }
    }
    else {
        stat = runActions();
    }
    if (expr_fast) {
        owner->endDeferredPropertyNotify();
    }
    if (stat == Failed) {
        std::stringstream ss;
        ss << owner->fullName() << ": " << command_name.get();
        if (last_step < actions.size() && actions[last_step]->aborted()) {
            ss << " " << *actions[last_step];
        }
        else {
            ss << " Failed to start an action: " << *this;
        }
        char *msg = strdup(ss.str().c_str());
        MessageLog::instance()->add(msg);
        NB_MSG << msg << "\n";
        error_str = msg;
        status = stat;
        owner->stop(this);
        return Failed;
    }
    else if (stat == Complete) {
        Action *curr = owner->executingCommand();
        if (curr && curr != this) {
            return curr->getStatus();
        }
        if (current_step == actions.size()) {
            status = Complete;
            owner->stop(this);
            //DBG_M_ACTIONS << " finished starting " << command_name.get() << "\n";
        }
    }
    return status;
}

Action::Status MachineCommand::checkComplete() {
    DBG_M_ACTIONS << "MachineCommand::checkComplete " << owner->getName() << "\n";
    if (status == Suspended) {
        resume();
    }
    if (status != Running) {
        return status;
    }
    while (current_step < actions.size()) {
        // currently running, attempt to move through the actions
        Action *a = actions[current_step];
        if (a->getStatus() == New || a->getStatus() == NeedsRetry) {
            if ((*a)() == NeedsRetry) {
                return Running;
            }
        }
        if (a->getStatus() == Complete) {
            ++current_step;
        }
        else if (a->getStatus() == Failed) {
            NB_MSG << command_name.get() << " " << a->error() << "\n";
            owner->stop(this);
            return status; // an action failed
        }
        else if (a->getStatus() == Running || a->getStatus() == Suspended) {
            if (a->complete()) { // check current status
                if (a->getStatus() == Failed) {
                    NB_MSG << command_name.get() << " " << a->error() << "\n";
                    owner->stop(this);
                    return status; // an action failed
                }
                else {
                    ++current_step; //the action is now complete.
                }
            }
            else {
                return a->getStatus(); // still running at step a
            }
        }
        status = runActions(); // note: this increments current_step
    }
    status = Complete;
    owner->stop(this);
    return status;
}

void MachineCommand::reset() { status = New; }
Action *MachineCommandTemplate::factory(MachineInstance *mi) {
    return new MachineCommand(mi, this);
}
