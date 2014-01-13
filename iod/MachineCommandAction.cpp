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
#include "DebugExtra.h"
#include "Logger.h"
#include "IOComponent.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include <sstream>

void MachineCommandTemplate::setActionTemplates(std::list<ActionTemplate*> &new_actions) {
    BOOST_FOREACH(ActionTemplate *at, new_actions) {
        action_templates.push_back(at);
    }
}
void MachineCommandTemplate::setActionTemplate(ActionTemplate *at) {
    action_templates.push_back(at);
}


MachineCommand::MachineCommand(MachineInstance *mi, MachineCommandTemplate *mct)
: Action(mi), command_name(mct->command_name), state_name(mct->state_name), timeout(0), timeout_trigger(0) {
    BOOST_FOREACH(ActionTemplate *t, mct->action_templates) {
        //DBG_M_ACTIONS << "copying action " << (*t) << " for machine " << mi->_name << "\n";
        actions.push_back(t->factory(mi));
    }
}

MachineCommand::~MachineCommand() {
	BOOST_FOREACH(Action *a, actions) { 
		owner->active_actions.remove(a); 
		if (a->getTrigger() && a->getTrigger()->enabled() && !a->getTrigger()->fired() )
			a->disableTrigger();
        a->release();
	}
    if (timeout_trigger) {
        timeout_trigger->disable();
        timeout_trigger->release();
    }
}

void MachineCommand::addAction(Action *a, ActionParameterList *params) { 
    actions.push_back(a);
}

void MachineCommand::setActions(std::list<Action*> &new_actions) {
    std::copy(new_actions.begin(), new_actions.end(), back_inserter(actions));
}


std::ostream &MachineCommand::operator<<(std::ostream &out)const {
	out << "Command " << owner->getName() << "." << command_name << "\n";
#if 0
	BOOST_FOREACH(Action *a, actions) {
		out << "   " << *a << "\n";
	}
#endif
	return out;
}

Action::Status MachineCommand::runActions() {
    while (current_step < actions.size()) {
        Action *a = actions[current_step]->retain();
		setBlocker(a);
		suspend();
		DBG_M_ACTIONS << owner->getName() << " about to execute " << *a << "\n";
		Action::Status stat = (*a)();
        if (stat == Action::Failed) {
            std::stringstream ss;
            ss << " action: " << *a <<" running on " << owner->getName() << " failed to start (" << a->error() << ")\n";
            char *err_msg = strdup(ss.str().c_str());
            MessageLog::instance()->add(err_msg);
			error_str = err_msg;
            return Failed; // action failed to start
        }
        if (stat == Action::NeedsRetry) {
            //NB_MSG << " action: " << *a << " failed temporarily (" << a->error() << ")\n";
            owner->stop(a);
            status = Running;
            return status; // action failed to start
        }
        if (!a->complete()) {
			DBG_M_ACTIONS << "leaving action: " << *a << " to run for a while\n";
            return stat; // currently running at curent_step
        }
		Action *x = owner->executingCommand();
		if (x==a) {
			DBG_M_ACTIONS << "action didn't remove itself " << *a << "\n";
			owner->stop(a);
		}
//		x = owner->executingCommand();
//		if (x==a) { DBG_M_ACTIONS << "stop doesn't work\n"; exit(2); }
		if (owner->executingCommand() != this) {
            std::stringstream ss;
            ss << "ERROR: command executing (" << *owner->executingCommand() <<") is not " << *this;
            char *err_msg = strdup(ss.str().c_str());
            MessageLog::instance()->add(err_msg);
			DBG_M_ACTIONS << err_msg << "\n";
            free(err_msg);
        }
		setBlocker(0);
        ++current_step;
        DBG_M_ACTIONS << owner->getName() <<  " completed action: " << *a << "\n";
    }
    DBG_M_ACTIONS << owner->getName() << " " << *this <<" completed all actions\n";
	//owner->needs_check = true; // conservative..
    return Complete;
}

Action::Status MachineCommand::run() { 
	owner->start(this);
	status = Running;
    current_step = 0;
    if (state_name.get() && strlen(state_name.get()) &&
        owner->getCurrent().getName() != state_name.get()) {
        std::stringstream ss;
        ss << "Command was ignored due to a mismatch of current state (" << owner->getCurrent().getName()
            << ") and state required by the command (" << state_name << ")";
        char *err_msg = strdup(ss.str().c_str());
        MessageLog::instance()->add(err_msg);
        DBG_M_ACTIONS << err_msg << "\n";
        result_str = err_msg;
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
    
    // attempt to run commands until one `blocks' on a timer
	Action::Status stat = runActions();
    if (stat  == Failed) {
        std::stringstream ss; ss << owner->getName() << ": " << command_name.get() << " Failed to start an action: " << *this;
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
	if (status != Running) return status;
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
        else if (a->getStatus() == Running || a->getStatus() == Suspended){
            return a->getStatus(); // still running at step a
        }
        status = runActions(); // note: this increments current_step 
    }
	status = Complete;
	owner->stop(this);
    return status;
}

void MachineCommand::reset() {
	status = New;
}
Action *MachineCommandTemplate::factory(MachineInstance *mi) {
    return new MachineCommand(mi, this);
}
