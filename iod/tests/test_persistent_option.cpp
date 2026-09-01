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

// PERSISTENT OPTION (iod-9): a per-field persist.dat marker. Verifies the
// MachineClass marks the field as persistent (and non-column, like LOCAL), and
// that a machine carrying such a field is persistent even without the
// machine-level `PERSISTENT` flag.

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

    MachineClass *mc = new MachineClass("Setpoint");
    mc->addPersistentProperty("sp"); // PERSISTENT OPTION sp 0
    mc->addPrivateProperty("tmp");   // LOCAL OPTION tmp 0

    if (!mc->hasPersistentProperties()) {
        std::cerr << "hasPersistentProperties() is false after addPersistentProperty\n";
        return 1;
    }
    if (!mc->propertyIsPersistent("sp")) {
        std::cerr << "propertyIsPersistent(sp) is false\n";
        return 2;
    }
    if (mc->propertyIsPersistent("tmp")) {
        std::cerr << "LOCAL field tmp wrongly reported persistent\n";
        return 3;
    }
    // PERSISTENT is non-column (like LOCAL), so it must be marked local too.
    if (!mc->propertyIsLocal("sp")) {
        std::cerr << "PERSISTENT field sp is not marked non-column (local)\n";
        return 4;
    }

    MachineInstance *m = MachineInstanceFactory::create("s", "Setpoint");
    m->setStateMachine(mc);
    machines[m->getName()] = m;

    // No PERSISTENT flag, but the class has a PERSISTENT OPTION: still persistent.
    if (!m->isPersistent()) {
        std::cerr << "machine with a PERSISTENT OPTION is not isPersistent()\n";
        return 5;
    }

    std::cout << "ok\n";
    return 0;
}
