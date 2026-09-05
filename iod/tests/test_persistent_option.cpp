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

#include "Channel.h"
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

    // Machine-level flag: OPTION PERSISTENT true sets the reserved "PERSISTENT"
    // option that isPersistent() reads.
    MachineClass *mc2 = new MachineClass("Panel");
    mc2->setOption("PERSISTENT", Value("true"));
    MachineInstance *m2 = MachineInstanceFactory::create("p", "Panel");
    m2->setStateMachine(mc2);
    machines[m2->getName()] = m2;
    if (!m2->isPersistent()) {
        std::cerr << "machine with OPTION PERSISTENT true is not isPersistent()\n";
        return 6;
    }
    // The PERSISTENCE_CHANNEL monitors `PERSISTENT == "true"` and matches with
    // machine->getValue("PERSISTENT") == Value("true", t_string) (Channel.cpp).
    // Verify the flag resolves to "true" under that exact comparison, so a
    // persistent machine is still selected and published to the channel.
    if (!(m2->getValue("PERSISTENT") == Value("true", Value::t_string))) {
        std::cerr << "OPTION PERSISTENT true machine not matched by channel monitor (PERSISTENT == \"true\")\n";
        return 8;
    }

    MachineClass *mc3 = new MachineClass("Transient");
    mc3->setOption("PERSISTENT", Value("false"));
    MachineInstance *m3 = MachineInstanceFactory::create("t", "Transient");
    m3->setStateMachine(mc3);
    machines[m3->getName()] = m3;
    if (m3->isPersistent()) {
        std::cerr << "machine with OPTION PERSISTENT false is wrongly isPersistent()\n";
        return 7;
    }

    // PERSISTENT OPTION fields are non-column (marked local) but must still be
    // published to the persistence channel, so the channel must NOT treat them
    // as local/private (which would skip them).
    if (Channel::isLocalOrPrivate(m, Value("sp"))) {
        std::cerr << "persistent field sp wrongly treated as local/private (not publishable)\n";
        return 9;
    }
    if (!Channel::isLocalOrPrivate(m, Value("tmp"))) {
        std::cerr << "LOCAL field tmp wrongly treated as publishable\n";
        return 10;
    }

    // The PERSISTENCE_CHANNEL (MONITORS PERSISTENT == "true") must select machines
    // that are persistent via per-field PERSISTENT OPTION, not only the flag.
    ChannelDefinition *cd = new ChannelDefinition("PERSISTENCE_CHANNEL");
    cd->addMonitorProperty("PERSISTENT", Value("true", Value::t_string));
    Channel *chn = Channel::create(7901, cd);
    if (!chn->channelMachines().count(m)) {
        std::cerr << "per-field PERSISTENT OPTION machine not selected by channel monitor\n";
        return 11;
    }
    if (!chn->channelMachines().count(m2)) {
        std::cerr << "OPTION PERSISTENT true machine not selected by channel monitor\n";
        return 12;
    }
    if (chn->channelMachines().count(m3)) {
        std::cerr << "OPTION PERSISTENT false machine wrongly selected by channel monitor\n";
        return 13;
    }

    // Modbus deferred flush: a modbus-exported property updated while property
    // notify is deferred must still be pushed to the modbus channel when the
    // defer ends (the deferred path skips sendPropertyChange but must not drop
    // the modbus update).
    {
        MachineClass *mc_mb = new MachineClass("Baler");
        MachineInstance *mb = MachineInstanceFactory::create("baler", "Baler");
        mb->setStateMachine(mc_mb);
        machines[mb->getName()] = mb;
        mb->addModbusExport("baler.BaleNo", ModbusAddress::holding_register, 1, mb,
                            ModbusExport::rw_reg, ModbusAddress::machine, "baler.BaleNo");
        mb->publish();
        mb->setNeedsThrottle(true);

        ChannelDefinition *mcd = new ChannelDefinition("MODBUS_CHANNEL");
        mcd->addFeature(ChannelDefinition::ReportModbusUpdates);
        mcd->setThrottleTime(50);
        Channel *mchn = Channel::create(7902, mcd);
        // Force the channel to ACTIVE so Channel::sendModbusUpdate dispatches to
        // it. Channel::setState sets current_state = ACTIVE first, then returns
        // Failed only because this minimal harness has no subscription manager;
        // the state is still ACTIVE, so the "failed" log is expected/harmless.
        mchn->setState(ChannelImplementation::ACTIVE);

        mb->beginDeferredPropertyNotify();
        mb->setValue("BaleNo", 5);
        mb->endDeferredPropertyNotify();

        if (mchn->pendingThrottledCount() == 0) {
            std::cerr << "deferred modbus update was not flushed to the modbus channel\n";
            return 14;
        }
    }

    std::cout << "ok\n";
    return 0;
}
