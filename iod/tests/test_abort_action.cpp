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

// Unit test for AbortAction's three outcomes:
//   RETURN -> AbortActionTemplate(false)  : Complete (successful), aborted
//   ABORT  -> AbortActionTemplate(true)   : Failed (unsuccessful), aborted
//   THROW  -> AbortActionTemplate(true, msg) : Failed + message, aborted
//
// These are the semantics Martin's WAITFOR/CALL `ON TIMEOUT ABORT|RETURN|THROW`
// design (RECORD_DB.md PR 11) depends on.

#include "AbortAction.h"
#include "Action.h"
#include "Dispatcher.h"
#include "Logger.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "ThreadSafeQueue.h"
#include "value.h"
#include "library_globals.cpp"
#include <boost/thread/mutex.hpp>
#include <iostream>
#include <zmq.hpp>

int main() {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    MessageLog::setMaxMemory(10000);
    boost::condition_variable_any cond;
    boost::shared_mutex mutex;
    SharedThreadSafeQueue<Package *> queue(cond, mutex);
    Dispatcher::create(queue);

    MachineClass *mc = new MachineClass("AbortTester");
    MachineInstance *m = MachineInstanceFactory::create("t", "AbortTester");
    m->setStateMachine(mc);
    machines[m->getName()] = m;

    // RETURN: abort_fail=false -> Complete (successful), action marked aborted.
    {
        AbortActionTemplate aat(false);
        Action *a = aat.factory(m);
        Action::Status st = (*a)();
        if (st != Action::Complete) {
            std::cerr << "RETURN returned " << actionStatusName(st) << ", expected Complete\n";
            return 1;
        }
        if (!a->aborted()) {
            std::cerr << "RETURN did not mark the action aborted\n";
            return 2;
        }
        delete a;
    }

    // ABORT: abort_fail=true -> Failed (unsuccessful), action marked aborted.
    {
        AbortActionTemplate aat(true);
        Action *a = aat.factory(m);
        Action::Status st = (*a)();
        if (st != Action::Failed) {
            std::cerr << "ABORT returned " << actionStatusName(st) << ", expected Failed\n";
            return 3;
        }
        if (!a->aborted()) {
            std::cerr << "ABORT did not mark the action aborted\n";
            return 4;
        }
        delete a;
    }

    // THROW: abort_fail=true with a message -> Failed + aborted; the message is
    // recorded on the action (delivery to a CATCH/RECEIVE handler is exercised
    // by tests/exceptions.cw).
    {
        AbortActionTemplate aat(true, Value("boom"));
        Action *a = aat.factory(m);
        Action::Status st = (*a)();
        if (st != Action::Failed) {
            std::cerr << "THROW returned " << actionStatusName(st) << ", expected Failed\n";
            return 5;
        }
        if (!a->aborted()) {
            std::cerr << "THROW did not mark the action aborted\n";
            return 6;
        }
        delete a;
    }

    std::cout << "abort action outcomes OK\n";
    return 0;
}
