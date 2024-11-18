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

#include "IOComponent.h"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include <assert.h>
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <zmq.hpp>

#include "cJSON.h"
#include <boost/thread/mutex.hpp>
#include <list>
#include <set>

#include "ClientInterface.h"
#include "DebugExtra.h"
#include "IODCommand.h"
#include "IODCommands.h"
#include "IOInterface.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "Statistic.h"
#include "Statistics.h"
#include "clockwork.h"
#include "options.h"
#include "symboltable.h"

#include "Channel.h"
#include "ControlSystemMachine.h"
#include "ProcessingThread.h"
#include "watchdog.h"
#include <pthread.h>
#include "SharedWorkSet.h"
#include "Dispatcher.h"
#include "Scheduler.h"

#include <iostream>

extern bool program_done;
extern bool machine_is_ready;
extern Statistics *statistics;
extern uint64_t client_watchdog_timer;
uint64_t clockwork_watchdog_timer = 0;
static pthread_t iod_thread_id;

extern void handle_io_sampling(uint64_t clock);

//#define KEEPSTATS

#define VERBOSE_DEBUG 0

unsigned int CommandSocketInfo::last_idx = 5;

class ProcessingThreadInternals {
  public:
    int sequence;
    long cycle_delay;
    IOUpdate update;

    static const int ECAT_ITEM = 0;       // ethercat data incoming
    static const int CMD_ITEM = 1;        // client interface time sync
    static const int SCHEDULER_ITEM = 2;  // scheduled items firing
    static const int ECAT_OUT_ITEM = 3;   //io has data update for ethercat
    static const int CMD_SYNC_ITEM = 4;   // client interface sending message

    Watchdog processing_wd;
    ClockworkProcessManager process_manager;
    std::list<CommandSocketInfo *> channel_sockets;

    ProcessingThreadInternals()
        : sequence(0), cycle_delay(1000), processing_wd("Processing Loop Watchdog", 2000) {}
};

ProcessingThread &ProcessingThread::create(ControlSystemMachine *m, HardwareActivation &activator,
                                           IODCommandThread &cmd_interface, SharedThreadSafeQueue<Package*> &queue) {
    if (!instance_) {
        instance_ = new ProcessingThread(m, activator, cmd_interface, queue);
    }
    return *instance_;
}

ProcessingThread::ProcessingThread(ControlSystemMachine *m, HardwareActivation &activator,
                                   IODCommandThread &cmd_interface, SharedThreadSafeQueue<Package*> &queue)
    : internals(0), machine(*m), status(e_waiting), activate_hardware(activator),
      command_interface(cmd_interface), message_queue(queue), program_start(0) {
    program_start = microsecs();
    internals = new ProcessingThreadInternals();
}

ProcessingThread::~ProcessingThread() { delete internals; }

void ProcessingThread::stop() {
    program_done = true;
    MessagingInterface::abort();
}

void ProcessingThread::join() {
    if (instance_) {
        instance_->join();
    }
}

CommandSocketInfo *ProcessingThread::addCommandChannel(CommandSocketInfo *csi) {
    std::list<CommandSocketInfo *>::iterator iter = internals->channel_sockets.begin();
    int idx = 0;
    while (iter != internals->channel_sockets.end()) {
        CommandSocketInfo *info = *iter++;
        ++idx;
        if (info == csi) {
            NB_MSG << "Processing thread already has command socket info for " << csi->address
                   << " at index " << idx << "\n";
            return csi; // already configured
        }
    }
    internals->channel_sockets.push_back(csi);
    return csi;
}

CommandSocketInfo *ProcessingThread::addCommandChannel(Channel *chn) {
    if (chn->definition()->isPublisher()) {
        return 0;
    }
    CommandSocketInfo *info = new CommandSocketInfo(chn);
    internals->channel_sockets.push_back(info);
    return info;
}

bool ProcessingThread::checkAndUpdateCycleDelay() {
    const Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
    long delay = 100;
    if (cycle_delay_v && cycle_delay_v->iValue >= 100) {
        delay = cycle_delay_v->iValue;
    }
    if (delay != internals->cycle_delay) {
        set_cycle_time(delay);
        internals->cycle_delay = delay;
        return true;
    }
    return false;
}

/*
    void ProcessingThread::waitForCommandProcessing(zmq::socket_t &resource_mgr)
    {
    // handshake to give the command handler access to shared resources for a while
    // if it has requested it.
    // first stage is to give access second stage is to assert we are taking access back

    safeSend(resource_mgr,"go", 2);
    status = e_waiting;
    char buf[10];
    size_t len = 0;
    safeRecv(resource_mgr, buf, 10, true, len);
    safeSend(resource_mgr,"bye", 3);
    }
*/

class IOLockHelper {
  public:
    IOLockHelper() { IOComponent::lock(); }
    ~IOLockHelper() { IOComponent::unlock(); }
};

IOUpdate receive_ethercat_data(zmq::socket_t &ecat_sync) {
    IOUpdate update;
    // the EtherCAT message carries a mask and data

    int64_t more;
    size_t more_size = sizeof(more);
    uint8_t stage = 1;
    //  Process all parts of the message
    while (true) {
        try {
            switch (stage) {
            case 1: { // global clock
                zmq::message_t message;
                // clock
                ecat_sync.recv(&message);
                size_t msglen = message.size();
                DBG_PROCESSING << "recv stage: " << (int)stage << " " << msglen << "\n";
                assert(msglen == sizeof(update.global_clock()));
                update.setGlobalClock(*reinterpret_cast<uint64_t*>(message.data()));
                ++stage;
            }
            case 2: { // data size
                zmq::message_t message;
                // data length
                ecat_sync.recv(&message);
                size_t msglen = message.size();
                if (msglen == 0) {
                    std::cerr << "Error: Got a null value for data size receiving ecat data\n";
                    stage = 4;
                    break;
                }
                assert(msglen == 4);
                DBG_PROCESSING << "recv stage: " << (int)stage << " " << msglen << "\n";
                uint32_t data_size = *reinterpret_cast<uint32_t*>(message.data());
                if (data_size != update.data_size()) {
                    DBG_PROCESSING << "Process data size updated. Was: " << update.data_size()
                              << " now: " << data_size << "\n";
                }
                ++stage;
            }
            case 3: { // data
                ecat_sync.getsockopt(ZMQ_RCVMORE, &more, &more_size);
                assert(more);
                zmq::message_t message;
                ecat_sync.recv(&message);
                size_t msglen = message.size();
                DBG_PROCESSING << "recv stage: " << (int)stage << " " << msglen << "\n";
                update.setData(static_cast<uint8_t*>(message.data()), msglen);
                ++stage;
            }
            case 4: { // mask
                zmq::message_t message;
                ecat_sync.getsockopt(ZMQ_RCVMORE, &more, &more_size);
                assert(more);
                ecat_sync.recv(&message);
                size_t msglen = message.size();
                DBG_PROCESSING << "recv stage: " << (int)stage << " " << msglen << "\n";
                update.setMask(static_cast<uint8_t*>(message.data()), msglen);
                ++stage;
                break;
            }
            default: {
                DBG_PROCESSING << "unexpected stage " << (int)stage << "\n";
                assert(stage <= 4);
            };
            }
            break;
        }
        catch (const zmq::error_t &ex) {
            if (zmq_errno() == EINTR) {
                std::stringstream err;
                err << "interrupted when receiving update at stage " << stage;
                MessageLog::instance()->add(err.str());
                continue;
            }
            else {
                NB_MSG << "Exception: " << ex.what()  << " "
                       << __FILE__ << ":" << __LINE__ << " (" << zmq_strerror(errno)
                       << ")\n";
            }
        }
    }
    return update;
}

int ProcessingThread::pollZMQItems(int poll_wait, zmq::pollitem_t *items, int num_items,
                                   zmq::socket_t &ecat_sync, zmq::socket_t &resource_mgr,
                                   zmq::socket_t &scheduler, zmq::socket_t &ecat_out) {
    int res = 0;
    while (!program_done) {
        try {
            long len = 0;
            int item_count = 0;
            res = zmq::poll(&items[0], num_items, poll_wait);
            if (!res) {
                return res;
            }
            for (int i = 0; i < num_items; i++) {
                if (items[i].revents & ZMQ_POLLIN) {
                    ++item_count;
                }
            }
            if (!item_count) { return 0; }
            if (items[internals->ECAT_ITEM].revents & ZMQ_POLLIN) {
                IOLockHelper lock;
                internals->update = receive_ethercat_data(ecat_sync);
                break;
            }
            break;
        }
        catch (const std::exception &ex) {
            if (errno == EINTR) {
                continue; // TBD watch for infinite loop here
            }
            const char *fnam = strrchr(__FILE__, '/');
            if (!fnam) {
                fnam = __FILE__;
            }
            else {
                fnam++;
            }
            NB_MSG << "Error " << ex.what() << " (" << zmq_strerror(errno) << ") in " << fnam << ":"
                   << __LINE__ << "\n";
            break;
        }
    }
    return res;
}

ProcessingThread *ProcessingThread::instance_ = 0;
ProcessingThread *ProcessingThread::instance() { return instance_; }
void ProcessingThread::setProcessingThreadInstance(ProcessingThread *pti) { instance_ = pti; }

void ProcessingThread::handle_package(Package *p) {
#ifdef KEEPSTATS
    AutoStat stats(avg_dispatch_time);
#endif
    DBG_DISPATCHER << "Dispatcher sending package " << *p << "\n";
    Receiver *to = p->receiver;
    Transmitter *from = p->transmitter;
    Message m(*p->message); //TBD is this copy necessary
    if (to) {
        MachineInstance *mi = dynamic_cast<MachineInstance *>(to);
        Channel *chn = dynamic_cast<Channel *>(to);
        if (chn) {
            DBG_DISPATCHER << "Channel message\n";
        }
        if (!mi->getStateMachine()) {
            char buf[100];
            snprintf(buf, 100,
                     "Warning: Machine %s does not have a valid state machine",
                     mi->getName().c_str());
            MessageLog::instance()->add(buf);
            NB_MSG << buf << "\n";
        }
        if (!chn && mi && mi->getStateMachine() &&
            mi->getStateMachine()->token_id == ClockworkToken::EXTERNAL) {
            DBG_DISPATCHER << "Dispatcher sending external message " << *p << " to "
                           << to->getName() << "\n";
            {
                // The machine has no parameters take the properties from the machine
                MachineInstance *remote = mi;
                if (mi->parameters.size() > 0) {
                    remote = mi->lookup(mi->parameters[0]);
                    // the host and port properties are specifed by the first parameter
                    char buf[100];
                    snprintf(
                        buf, 100,
                        "Error dispatching message,  EXTERNAL configuration: %s not found",
                        mi->parameters[0].val.sValue.c_str());
                    MessageLog::instance()->add(buf);
                    NB_MSG << buf << "\n";
                }
                Value host = remote->properties.lookup("HOST");
                Value port_val = remote->properties.lookup("PORT");
                Value protocol = mi->properties.lookup("PROTOCOL");
                int64_t port;
                if (port_val.asInteger(port)) {
                    if (protocol == "RAW") {
                        MessagingInterface *mif = MessagingInterface::create(
                            host.asString(), (int)port, eRAW);
                        if (!mif->started()) {
                            mif->start();
                        }
                        mif->send_raw(m.getText().c_str());
                    }
                    else {
                        if (protocol == "CLOCKWORK") {
                            MessagingInterface *mif = MessagingInterface::create(
                                host.asString(), (int)port, eCLOCKWORK);
                            if (!mif->started()) {
                                mif->start();
                            }
                            DBG_DISPATCHER << "sending (CLOCKWORK): " << m.getText()
                                           << "\n";
                            mif->send(m);
                        }
                        else {
                            MessagingInterface *mif = MessagingInterface::create(
                                host.asString(), (int)port, eZMQ);
                            mif->start();
                            DBG_DISPATCHER << "sending: " << m.getText() << "\n";
                            mif->send(m.getText().c_str());
                        }
                    }
                }
            }
        }
        else if (chn) {
            // when sending to a channel, if the channel has a publisher, get it to send the message
            DBG_DISPATCHER << "Dispatcher sending " << *p << " to channel "
                           << to->getName() << "\n";
            MessagingInterface *mif = chn->getPublisher();
            if (mif) {
                Value protocol = mi->properties.lookup("PROTOCOL");
                if (protocol == "RAW") {
                    mif->send_raw(m.getText().c_str());
                }
                else {
                    if (protocol == "CLOCKWORK") {
                        DBG_DISPATCHER << "sending to " << mif->getName()
                                       << " (CLOCKWORK): " << m.getText() << "\n";
                        mif->send(m);
                    }
                    else {
                        mif->send(m.getText().c_str());
                    }
                }
            }
        }
        else {
            DBG_DISPATCHER << "Dispatcher queued " << *p << " to " << to->getName()
                           << "\n";
            to->enqueue(*p);
            //MachineInstance::forceIdleCheck();
            MachineInstance *mi = dynamic_cast<MachineInstance *>(to);
            if (mi) {
                SharedWorkSet::instance()->add(mi);
                mi->set_runnable(true);
                Action *curr = mi->executingCommand();
                if (curr) {
                    DBG_DISPATCHER << mi->getName() << " currently executing "
                                   << *curr << "\n";
                }
            }
        }
    }
    else {
        auto receivers = Dispatcher::instance()->all_receivers.lock();
        auto iter = receivers.begin();
        while (iter != receivers.end()) {
            Receiver *r = *iter++;
            if (r->receives(m, from)) {
                r->enqueue(*p);
            }
        }
        Dispatcher::instance()->all_receivers.unlock();
    }
}

void ProcessingThread::HandleIncomingEtherCatData(std::set<IOComponent *> &io_work_queue,
                                                  uint64_t curr_t, uint64_t last_sample_poll,
                                                  AutoStatStorage &avg_io_time) {
    IOLockHelper io_lock;
#ifdef KEEPSTATS
    static unsigned long total_mp_time = 0;
    static unsigned long mp_count = 0;
#endif

    if (machine_is_ready) {
        AutoStat stats(avg_io_time);
        IOComponent::processAll(internals->update, io_work_queue);
    }
    else {
        std::cout << "Processing received EtherCAT data but machine is not ready\n";
    }
    if (curr_t - last_sample_poll >= 10000) {
        last_sample_poll = curr_t;
        handle_io_sampling(internals->update.global_clock()); // devices that need a regular poll
    }
}

ProcessingThread::ProcessingState ProcessingThread::poll_machines() {
#ifdef KEEPSTATS
    avg_clockwork_time.start();
#endif
    std::set<MachineInstance *> to_process;
    {
        auto x = MachineInstance::begin();
        while (x != MachineInstance::end()) {
            if ((*x)->is_runnable()) {
                runnable.insert(*x);
                (*x)->set_runnable(false);
            }
            ++x;
        }

        std::set<MachineInstance *>::iterator iter = runnable.begin();
        while (iter != runnable.end()) {
            MachineInstance *mi = *iter;
            if (mi->executingCommand() || !mi->pendingEvents().empty() || mi->hasMail()) {
                to_process.insert(mi);
                if (!mi->queuedForStableStateTest()) {
                    iter = runnable.erase(iter);
                }
                else {
                    iter++;
                }
            }
            else {
                iter++;
            }
        }
    }

    if (!to_process.empty()) {
        DBG_SCHEDULER << "processing " << to_process.size() << " machines\n";
        MachineInstance::processAll(to_process, 150000, MachineInstance::NO_BUILTINS);
    }
    else {
        DBG_MSG << "machines don't need poll\n";
    }
    return eStableStates;
}

void ProcessingThread::operator()() {

    iod_thread_id = pthread_self();
#ifdef __APPLE__
    pthread_setname_np("iod processing");
#else
    pthread_setname_np(pthread_self(), "iod processing");
#endif

    Statistic *cycle_delay_stat = new Statistic("Cycle Delay");
    Statistic::add(cycle_delay_stat);
    long delta, delta2;

    AutoStatStorage avg_io_time("AVG_IO_TIME", 0);
    AutoStatStorage avg_update_time("AVG_UPDATE_TIME", 0);
#ifdef KEEPSTATS
    AutoStatStorage avg_poll_time("AVG_POLL_TIME", 0);
    AutoStatStorage avg_iowork_time("AVG_IOWORK_TIME", 0);
    AutoStatStorage avg_plugin_time("AVG_PLUGIN_TIME", 0);
    AutoStatStorage avg_cmd_processing("AVG_PROCESSING_TIME", 0);
    AutoStatStorage avg_command_time("AVG_COMMAND_TIME", 0);
    AutoStatStorage avg_channel_time("AVG_CHANNEL_TIME", 0);
    AutoStatStorage avg_dispatch_time("AVG_DISPATCH_TIME", 0);
    AutoStatStorage avg_scheduler_time("AVG_SCHEDULER_TIME", 0);
    AutoStatStorage avg_clockwork_time("AVG_CLOCKWORK_TIME", 0);
    AutoStatStorage scheduler_delay("SCHEDULER_POLL_SEPARATION", 0);
#endif

    zmq::socket_t sched_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    sched_sync.connect("inproc://scheduler_sync");

    // used to permit command processing
    zmq::socket_t resource_mgr(*MessagingInterface::getContext(), ZMQ_PAIR);
    resource_mgr.connect("inproc://resource_mgr");

    zmq::socket_t ecat_sync(*MessagingInterface::getContext(), ZMQ_PAIR);
    ecat_sync.connect("inproc://ethercat_sync");

    zmq::socket_t command_sync(*MessagingInterface::getContext(), ZMQ_PAIR);
    command_sync.connect("inproc://command_sync");

    activate_hardware();

    Channel::initialiseChannels();

    safeSend(sched_sync, "go", 2); // scheduled items
    usleep(10000);
    Dispatcher::instance()->sync_start();
    usleep(10000);

    DBG_INITIALISATION << "----------- Enabling client access --------\n";
    safeSend(resource_mgr, "start", 5);

    safeSend(ecat_sync, "go", 2); // collect state
    usleep(10000);

    zmq::socket_t ecat_out(*MessagingInterface::getContext(), ZMQ_PAIR);
    ecat_out.connect("inproc://ethercat_output");

    checkAndUpdateCycleDelay();

    uint64_t last_checked_cycle_time = 0;
    uint64_t last_checked_plugins = 0;
    uint64_t last_checked_machines = 0;

    unsigned long total_cmd_time = 0;
    unsigned long cmd_count = 0;
    unsigned long total_sched_time = 0;
    unsigned long sched_count = 0;

    uint64_t start_cmd = 0;
    uint64_t last_machine_change = 0;

    MachineInstance *system = MachineInstance::find("SYSTEM");
    assert(system);

    UpdateStates update_state = UpdateStates::s_update_idle;

    bool commands_started = false;

    ProcessingState processing_state = eIdle;
    std::set<IOComponent *> io_work_queue;

    //  we need to stop polling io (ie exit) if the control threads do not seem
    // to be responding.
    const int MAX_UNCONTROLLED_POLLS = 5;
    int io_unsafe_polls_remaining = MAX_UNCONTROLLED_POLLS;
    while (!program_done) {
        if (IOComponent::getHardwareState() == IOComponent::s_hardware_preinit) {
            IOLockHelper io_lock;
            // attempt to initialise the hardware interface. If this
            // works we move the IOComponent module's state along
            // so that IOComponents can be linked
            internals->update.clear();
            if (activate_hardware.initialiseHardware()) {
                std::cout << "setting hardware state to init\n";
                IOComponent::setHardwareState(IOComponent::s_hardware_init);
            }
        }

        MachineInstance::remove_pending();
        uint64_t machine_check_delay = 0;
        machine.idle();
        last_machine_change = machine.lastUpdated();

        // only process io components if the machine is operational
        if (machine.c_operational()) {
            if (!machine_is_ready) {
                DBG_INITIALISATION << "machine is becoming ready\n";
                machine_is_ready = true;
            }
        }
        else {
            if (machine_is_ready) {
                DBG_INITIALISATION << "machine is no longer ready\n";
                machine_is_ready = false;
            }
        }

#ifdef KEEPSTATS
        avg_poll_time.start();
#endif

        zmq::pollitem_t fixed_items[] = {
            {(void *)ecat_sync, 0, ZMQ_POLLIN, 0},
            {(void *)resource_mgr, 0, ZMQ_POLLIN, 0},
            {(void *)sched_sync, 0, ZMQ_POLLIN, 0},
            {(void *)ecat_out, 0, ZMQ_POLLIN, 0},
            {(void *)command_sync, 0, ZMQ_POLLIN, 0}};
        const int max_poll_sockets = 25;
        zmq::pollitem_t items[max_poll_sockets];
        memset((void *)items, 0, max_poll_sockets * sizeof(zmq::pollitem_t));
        int dynamic_poll_start_idx = 5;

        int poll_wait = static_cast<int>(internals->cycle_delay / 1000); // millisecs
        machine_check_delay = internals->cycle_delay / 5;
        long systems_waiting = 0;
        uint64_t curr_t = 0;
        uint64_t last_sample_poll = 0;
        bool machines_have_work = true; // TODO: determine whether machine have work
        unsigned int num_channels = 0;
        while (!program_done) {
            {
                std::list<Package*> to_handle;
                Package *p;
                // TODO: Avoid this and exchange the list instead
                while (message_queue.try_dequeue(p)) {
                    to_handle.push_back(p);
                }
                while (!to_handle.empty()) {
                    p = to_handle.front();
                    to_handle.pop_front();
                    handle_package(p);
                    delete p;
                }
            }
            curr_t = nowMicrosecs();
            internals->process_manager.SetTime(curr_t);
            //TBD add a guard here to detect/prevent rapid cycling

            for (int i = 0; i < dynamic_poll_start_idx; ++i) {
                items[i] = fixed_items[i];
            }

            // add the channel sockets to our poll info
            {
                std::list<CommandSocketInfo *>::iterator csi_iter =
                    internals->channel_sockets.begin();
                int idx = dynamic_poll_start_idx;
                while (csi_iter != internals->channel_sockets.end()) {
                    CommandSocketInfo *info = *csi_iter++;
                    items[idx].socket = (void *)(*info->sock);
                    items[idx].fd = 0;
                    items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;
                    items[idx].revents = 0;
                    idx++;
                    if (idx == max_poll_sockets) {
                        break;
                    }
                }
                num_channels =
                    idx - dynamic_poll_start_idx; // the number channels we are actually monitoring
            }

            if (machines_have_work || IOComponent::updatesWaiting() || !io_work_queue.empty()) {
                poll_wait = 1;
            }
            else {
                poll_wait = 100;
            }

            //if (Watchdog::anyTriggered(curr_t))
            //  Watchdog::showTriggered(curr_t, true, std::cerr);
            systems_waiting = pollZMQItems(poll_wait, items, 5 + num_channels, ecat_sync,
                                           resource_mgr, sched_sync, ecat_out);

            if (systems_waiting > 0 ||
                (machines_have_work && curr_t - last_checked_machines >= machine_check_delay)) {
                break;
            }
            if (IOComponent::updatesWaiting() || !io_work_queue.empty()) {
                break;
            }
            if (!MachineInstance::pluginMachines().empty() &&
                curr_t - last_checked_plugins >= 1000) {
                break;
            }
            if (curr_t - last_machine_change > 10000) {
                last_machine_change = curr_t;
                machine.idle();
            }
            if (last_machine_change < machine.lastUpdated()) {
                break;
            }
#ifdef KEEPSTATS
            avg_poll_time.update();
            usleep(1);
            avg_poll_time.start();
#endif
        }

#ifdef KEEPSTATS
        avg_poll_time.update();
        avg_poll_time.start();
#endif
        /*  this loop prioritises ethercat processing but if a certain
            number of ethercat cycles have been processed with no
            other activities being given time, we give other jobs
            some time anyway.
        */
        if (items[internals->ECAT_ITEM].revents & ZMQ_POLLIN) {
            HandleIncomingEtherCatData(io_work_queue, curr_t, last_sample_poll, avg_io_time);
            update_state = UpdateStates::s_update_idle;
        }
        if (program_done) { break; }
        //if (machine_is_ready && processing_state != eStableStates && !io_work_queue.empty()) {
        if (machine_is_ready && !io_work_queue.empty()) {
#ifdef KEEPSTATS
            AutoStat stats(avg_iowork_time);
#endif
            std::set<IOComponent *>::iterator io_work = io_work_queue.begin();
            while (io_work != io_work_queue.end()) {
                IOComponent *ioc = *io_work;
                io_work = io_work_queue.erase(io_work);
                ioc->handleChange(MachineInstance::pendingEvents());
            }
        }

        if (program_done) {
            break;
        }
        if (!MachineInstance::pluginMachines().empty()) {
            if (processing_state == eIdle && curr_t - last_checked_plugins >= 1000) {
#ifdef KEEPSTATS
                AutoStat stats(avg_plugin_time);
#endif
                MachineInstance::checkPluginStates();
                last_checked_plugins = curr_t;
            }
        }
        else {
            last_checked_plugins = curr_t;
        }

        if (status == e_waiting) {
#ifdef KEEPSTATS
            AutoStat stats(avg_channel_time);
#endif
            Channel::handleChannels();
        }

        if (program_done) { break; }
        char buf[200];
        if (status == e_waiting) {
            if (items[internals->CMD_ITEM].revents & ZMQ_POLLIN) {
#ifdef KEEPSTATS
                AutoStat stats(avg_cmd_processing);
#endif
                size_t len = resource_mgr.recv(buf, 200, ZMQ_NOBLOCK);
                if (len) {
                    MessageLog::instance()->get_stream()
                        << "Warning: processing thread ignoring incoming data '" << buf
                        << "' from client";
                    MessageLog::instance()->release_stream();
                }
            }
        }

        if (program_done) {
            break;
        }

        if (status == e_waiting && systems_waiting > 0) {
            // check the command interface and any command channels for activity
            bool have_command = false;
            if (items[internals->CMD_SYNC_ITEM].revents & ZMQ_POLLIN) {
                have_command = true;
            }
            else {
                for (unsigned int i = dynamic_poll_start_idx;
                     i < dynamic_poll_start_idx + num_channels; ++i) {
                    if (items[i].revents & ZMQ_POLLIN) {
                        have_command = true;
                        break;
                    }
                }
            }
            if (have_command) {
                uint64_t start_time = microsecs();
                uint64_t now = start_time;
#ifdef KEEPSTATS
                AutoStat stats(avg_cmd_processing);
#endif
                [[maybe_unused]] int count = 0;
                while (have_command && (long)(now - start_time) < internals->cycle_delay / 2) {
                    have_command = false;
                    std::list<CommandSocketInfo *>::iterator csi_iter =
                        internals->channel_sockets.begin();
                    unsigned int i = internals->CMD_SYNC_ITEM;
                    while (i <= CommandSocketInfo::lastIndex() &&
                           (long)(now - start_time) < internals->cycle_delay / 2) {
                        zmq::socket_t *sock = 0;
                        CommandSocketInfo *info = 0;
                        if (i == internals->CMD_SYNC_ITEM) {
                            sock = &command_sync;
                        }
                        else {
                            if (csi_iter == internals->channel_sockets.end()) {
                                break;
                            }
                            info = *csi_iter++;
                            sock = info->sock;
                        }
                        { int rc = zmq::poll(&items[i], 1, 0); }
                        if (!(items[i].revents & ZMQ_POLLIN)) {
                            ++i;
                            continue;
                        }
                        have_command = true;

                        zmq::message_t msg;
                        char *buf = nullptr;
                        size_t len = 0;
                        MessageHeader mh;
                        uint32_t default_id = mh.getId(); // save the msgid to following check
                        if (safeRecv(*sock, &buf, &len, false, 0, mh)) {
                            ++count;
                            if (false && len > 10) {
                                FileLogger fl(program_name);
                                fl.f() << "Processing thread received command ";
                                if (buf) {
                                    fl.f() << buf << " ";
                                }
                                else {
                                    fl.f() << "NULL";
                                }
                                fl.f() << "\n";
                            }
                            if (!buf) {
                                continue;
                            }
                            IODCommand *command = parseCommandString(buf);
                            if (command) {
                                bool ok = false;
                                try {
                                    ok = (*command)();
                                }
                                catch (const std::exception &e) {
                                    FileLogger fl(program_name);
                                    fl.f() << "command execution threw an exception " << e.what()
                                           << "\n";
                                }
                                delete[] buf;

                                if (mh.needsReply() || mh.getId() == default_id) {
                                    char *response =
                                        strdup((ok) ? command->result() : command->error());
                                    MessageHeader rh(mh);
                                    rh.source = mh.dest;
                                    rh.dest = mh.source;
                                    rh.start_time = microsecs();
                                    safeSend(*sock, response, strlen(response), rh);
                                    free(response);
                                }
                                else {
                                    //char *response = strdup(command->result());
                                    //safeSend(*sock, response, strlen(response));
                                    //free(response);
                                }
                            }
                            else {
                                if (mh.needsReply() || mh.getId() == default_id) {
                                    char *response = new char[len + 40];
                                    snprintf(response, len + 40, "Unrecognised command: %s", buf);
                                    MessageHeader rh(mh);
                                    rh.source = mh.dest;
                                    rh.dest = mh.source;
                                    rh.start_time = microsecs();
                                    safeSend(*sock, response, strlen(response), rh);
                                    delete[] response;
                                }
                                else {
                                    /*
                                        char *response = new char[len+40];
                                        snprintf(response, len+40, "Unrecognised command: %s", buf);
                                        safeSend(*sock, response, strlen(response));
                                        delete[] response;
                                    */
                                }
                                delete[] buf;
                            }
                            delete command;
                        }
                        ++i;
                    }
                    usleep(0);
                    now = microsecs();
                }
            }
        }

        if (items[internals->SCHEDULER_ITEM].revents & ZMQ_POLLIN) {
#ifdef KEEPSTATS
            if (!scheduler_delay.running()) {
                scheduler_delay.start();
            }
#endif
            if (status == e_waiting && processing_state == eIdle) {
                size_t len = safeRecv(sched_sync, buf, 10, false, len, 0);
                if (len) {
                    status = e_handling_sched;
#ifdef KEEPSTATS
                    scheduler_delay.stop();
                    avg_scheduler_time.start();
#endif
                }
                else {
                    char buf[100];
                    snprintf(buf, 100, "WARNING: scheduler sync returned zero length message");
                    MessageLog::instance()->add(buf);
                }
            }
            else if (status == e_waiting_sched) {
                size_t len = safeRecv(sched_sync, buf, 10, false, len, 0);
                if (len) {
                    safeSend(sched_sync, "bye", 3);
                    status = e_waiting;
#ifdef KEEPSTATS
                    avg_scheduler_time.update();
#endif
                }
                else {
                    char buf[100];
                    snprintf(buf, 100, "WARNING: scheduler sync returned zero length message");
                    MessageLog::instance()->add(buf);
                }
            }
        }
        if (status == e_handling_sched) {
            size_t len = 0;
            safeSend(sched_sync, "continue", 8);
            status = e_waiting_sched;
        }

        if (machine.activationRequested()) {
            DBG_PROCESSING << " activation requested\n"
              << "status: " << status
              << " have devices: " << !IOComponent::devices.empty()
              << " update_status " << static_cast<int>(update_state)
              << "\n";
        }

        if (status == e_waiting && machines_have_work &&
            curr_t - last_checked_machines >= machine_check_delay) {

            if (processing_state == eIdle) {
                processing_state = ePollingMachines;
            }
            const int num_loops = 1;
            for (int i = 0; i < num_loops; ++i) {
                if (processing_state == ePollingMachines) {
                    processing_state = poll_machines();
                }
                if (processing_state == eStableStates) {
                    std::set<MachineInstance *> to_process;
                    {
                        auto x = MachineInstance::begin();
                        while (x != MachineInstance::end()) {
                            if ((*x)->is_runnable()) {
                                runnable.insert(*x);
                                (*x)->set_runnable(false);
                            }
                            ++x;
                        }
                        std::set<MachineInstance *>::iterator iter = runnable.begin();
                        while (iter != runnable.end()) {
                            MachineInstance *mi = *iter;
                            if (mi->executingCommand() || !mi->pendingEvents().empty()) {
                                iter++;
                                continue;
                            }
                            if (mi->queuedForStableStateTest()) {
                                to_process.insert(mi);
                                iter = runnable.erase(iter);
                            }
                            else {
                                iter++;
                            }
                        }
                    }

                    //if (!to_process.empty()) {
                        DBG_SCHEDULER << "processing stable states\n";
                        MachineInstance::checkStableStates(to_process, 150000);
                    //}
                    if (i < num_loops - 1) {
                        processing_state = ePollingMachines;
                    }
                    else {
                        processing_state = eIdle;
                        last_checked_machines = curr_t; // check complete
#ifdef KEEPSTATS
                        avg_clockwork_time.update();
#endif
                    }
                }
            }
        }
        if (machine.activationRequested()) {
            DBG_MSG << "activation requested, status == e_waiting?: " << (status == e_waiting)
                    << " device list empty?: " << IOComponent::devices.empty()
                    << " update_state == s_update_idle?: " << (update_state == UpdateStates::s_update_idle)
                    << "\n";
        }
        // send a message to the ethercat thread requesting activation
        // or deactivation of the master
        if (status == e_waiting && !IOComponent::devices.empty() && update_state == UpdateStates::s_update_idle &&
            (machine.activationRequested() || machine.deactivationRequested())) {
            DBG_INITIALISATION << "activation/deactivation requested\n";
            uint32_t size = 0;
            uint8_t stage = 1;
            while (true) {
                try {
                    switch (stage) {
                        case 1: {
                            zmq::message_t iomsg(4);
                            memcpy(iomsg.data(), (void *)&size, 4);
                            ecat_out.send(iomsg, ZMQ_SNDMORE);
                            ++stage;
                        }
                        case 2: {
                            auto packet_type = machine.activationRequested()
                                                   ? IOInterface::MessageType::ACTIVATE_REQUEST
                                                   : IOInterface::MessageType::DEACTIVATE_REQUEST;
                            zmq::message_t iomsg(1);
                            memcpy(iomsg.data(), (void *)&packet_type, 1);
                            ecat_out.send(iomsg);
                            ++stage;
                        }
                    }
                    std::cout << "Send hardware activate/deactivate request\n";
                    update_state = UpdateStates::s_update_sent;
                    break;
                }
                catch (const zmq::error_t &err) {
                    if (zmq_errno() == EINTR) {
                        DBG_PROCESSING << "interrupted when sending update (" << (unsigned int)stage
                                       << ")\n";
                        continue;
                    }
                    else {
                        std::cerr << zmq_strerror(zmq_errno());
                    }
                    assert(false);
                }
            }
        }
        else if (status == e_waiting && machine_is_ready && !IOComponent::devices.empty() &&
                 (update_state == UpdateStates::s_update_idle) &&
                 (IOComponent::updatesWaiting() ||
                  IOComponent::getHardwareState() != IOComponent::s_operational)) {
        static bool defaults_sent = false;
            avg_update_time.start();
            if (update_state == UpdateStates::s_update_idle) {
                IOUpdate &upd = internals->update;
                if (IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
                    DBG_INITIALISATION << "Sending defaults to EtherCAT\n";
                    upd = IOComponent::getDefaults();
                }
                else {
                    upd = IOComponent::getUpdates();
                }
                if (upd.data_size() > 0) {
                    uint32_t size = upd.data_size();
                    uint8_t stage = 1;
                    while (true) {
                        try {
                            switch (stage) {
                            case 1: {
                                zmq::message_t iomsg(4);
                                memcpy(iomsg.data(), (void *)&size, 4);
                                ecat_out.send(iomsg, ZMQ_SNDMORE);
                                ++stage;
                            }
                            case 2: {
                                auto packet_type = IOInterface::MessageType::PROCESS_DATA;
                                if (IOComponent::getHardwareState() != IOComponent::s_operational) {
                                    packet_type = IOInterface::MessageType::DEFAULT_DATA;
                                }
                                zmq::message_t iomsg(1);
                                memcpy(iomsg.data(), (void *)&packet_type, 1);
                                ecat_out.send(iomsg, ZMQ_SNDMORE);
                                ++stage;
                            }
                            case 3: {
                                zmq::message_t iomsg(size);
                                memcpy(iomsg.data(), (void *)upd.data(), size);
                                ecat_out.send(iomsg, ZMQ_SNDMORE);
#if VERBOSE_DEBUG
                                DBG_ETHERCAT << "sending to EtherCAT: ";
                                display(upd->data());
                                std::cout << "\n";
#endif
                                ++stage;
                            }
                            case 4: {
                                zmq::message_t iomsg(size);
                                memcpy(iomsg.data(), (void *)upd.mask(), size);
#if VERBOSE_DEBUG
                                DBG_ETHERCAT << "using mask: ";
                                display(std::cout, upd->mask());
                                std::cout << "\n";
#endif
                                ecat_out.send(iomsg);
                                ++stage;
                            }
                            default:;
                            }
                            break;
                        }
                        catch (const zmq::error_t &err) {
                            if (zmq_errno() == EINTR) {
                                DBG_PROCESSING << "interrupted when sending update ("
                                               << (unsigned int)stage << ")\n";
                                continue;
                            }
                            else {
                                std::cerr << zmq_strerror(zmq_errno());
                            }
                            assert(false);
                        }
                    }
                    IOComponent::updatesSent(true);
                }
                else {
                    std::cout << "udpate data is empty\n";
                }
                if (!defaults_sent) {
                    defaults_sent = true;
                    IOComponent::setHardwareState(IOComponent::s_operational);
                }
                update_state = UpdateStates::s_update_sent;
            }
        }
        //static bool should_be_op = false;
        if (update_state == UpdateStates::s_update_sent) {
            char buf[10];
            try {
                if (ecat_out.recv(buf, 10, ZMQ_DONTWAIT)) {
                    std::cout << "got " << buf << " from ecat when activation requested: "
                           << machine.activationRequested() << "\n";
                    update_state = UpdateStates::s_update_idle;
                    if (machine.activationRequested()) {
                        if (strncmp(buf, "ok", 2) == 0) {
                            machine.requestActivation(false);
                            std::cout << "should set operational mode\n";
                            //should_be_op = true;
                        }
                    }
                    else if (machine.deactivationRequested()) {
                        if (strncmp(buf, "ok", 2) == 0) {
                            machine.requestDeactivation(false);
                        }
                    }
                    else {
                        if (IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
                            std::cout << "setting hardware state to operational\n";
                            IOComponent::setHardwareState(IOComponent::s_operational);
                            assert(false && "set hardware state to op");
                        }
                    }
                }
#if 0
                else {
                    if (should_be_op && machine_is_ready 
                           && IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
                        std::cout << "setting hardware state to operational\n";
                        IOComponent::setHardwareState(IOComponent::s_operational);
                    }
                    //std::cout << "should be op: " << should_be_op 
                    //                << " machine_is_ready " << machine_is_ready
                    //                << " hardware state: " << IOComponent::getHardwareState()
                    //                << "\n";
                }
#endif
                avg_update_time.update();
            }
            catch (const zmq::error_t &err) {
                if (zmq_errno() != EINTR) {
                    NB_MSG << "Exception: " << err.what() << " (" << zmq_strerror(errno) << ")\n";
                }
                assert(zmq_errno() == EINTR);
            }
        }
#if 0
        else if (should_be_op && IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
            std::cout << "haven't set operational mode (update_state == " << (int)update_state << ")\n";
        }
        else if (should_be_op && IOComponent::getHardwareState() != IOComponent::s_operational) {
            std::cout << "hardware state is now: " << IOComponent::getHardwareState() << "\n";
        }
#endif

        // periodically check to see if the cycle time has been changed
        // more work is needed here since the signaller needs to be told about this
        static int cycle_check_counter = 0;
        if (++cycle_check_counter > 100) {
            cycle_check_counter = 0;
            checkAndUpdateCycleDelay();
        }

        machine.idle(); // in case any of the above triggered a change to the machine state
        last_machine_change = machine.lastUpdated();
        if (program_done) {
            break;
        }
    }
    const auto &log = MessageLog::instance();
    if (log->count() > 0) {
        std::cerr << "Messages at shutdown:\n" << log->toString(log->count()) << "\n";
    }
    DBG_INITIALISATION << "processing done\n";
}
