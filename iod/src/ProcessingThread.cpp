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
#include "ECInterface.h"
#include "ProcessingThread.h"
#include "watchdog.h"
#include <pthread.h>
#include "SharedWorkSet.h"
#include "Dispatcher.h"
#include "Scheduler.h"
#include "Trigger.h"
#include <sstream>

#include <chrono>
#include <iostream>
#include <malloc.h>

extern bool program_done;
extern bool machine_is_ready;
extern "C" long cJSON_LiveNodeCount(void);
extern Statistics *statistics;
extern uint64_t client_watchdog_timer;
uint64_t clockwork_watchdog_timer = 0;

extern void handle_io_sampling(uint64_t clock);

//#define KEEPSTATS

#define VERBOSE_DEBUG 0

unsigned int CommandSocketInfo::last_idx = 5;

class ProcessingThreadInternals {
  public:
    int sequence;
    long cycle_delay;

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
                                           IODCommandThread &cmd_interface, SharedThreadSafeQueue<Package*> &queue,
                                           SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> &mqtt_source_queue) {
    if (!instance_) {
        instance_ = new ProcessingThread(m, activator, cmd_interface, queue, mqtt_source_queue);
    }
    return *instance_;
}

ProcessingThread::ProcessingThread(ControlSystemMachine *m, HardwareActivation &activator,
                                   IODCommandThread &cmd_interface, SharedThreadSafeQueue<Package*> &queue,
                                   SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> &mqtt_source_queue)
    : internals(0), machine(*m), status(e_waiting), activate_hardware(activator),
      command_interface(cmd_interface), message_queue(queue), mqtt_source_queue(mqtt_source_queue), program_start(0) {
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
    // SYSTEM.POLLING_DELAY = Clockwork processing poll period (µs).
    // SYSTEM.CYCLE_DELAY is EtherCAT-only (handled by ecat thread / activate).
    long delay = 1000;
    const Value *poll_v = MachineInstance::polling_delay;
    if (poll_v && poll_v->iValue >= 100) {
        delay = poll_v->iValue;
    }
    else {
        Value *def = ClockworkInterpreter::instance()
                         ? ClockworkInterpreter::instance()->default_poll_delay
                         : nullptr;
        if (def && def->iValue >= 100) {
            delay = def->iValue;
        }
    }
    if (delay != internals->cycle_delay) {
        internals->cycle_delay = delay;
        set_polling_time(static_cast<unsigned long>(delay));
        DBG_INITIALISATION << "Clockwork POLLING_DELAY -> " << delay << " us ("
                           << (1000000 / delay) << " Hz process-data pull)\n";
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

static uint8_t *incoming_process_data = 0;
static uint8_t *incoming_process_mask = 0;
static uint32_t incoming_data_size;
static uint64_t global_clock = 0;

#if VERBOSE_DEBUG
static void display(std::ostream &out, uint8_t *p) {
    int max = IOComponent::getMaxIOOffset();
    int min = IOComponent::getMinIOOffset();
    for (int i = min; i <= max; ++i) {
        out << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i] << std::dec;
    }
}
#endif

class IOLockHelper {
  public:
    IOLockHelper() { IOComponent::lock(); }
    ~IOLockHelper() { IOComponent::unlock(); }
};

int ProcessingThread::pollZMQItems(int poll_wait, zmq::pollitem_t items[], int num_items,
                                   zmq::socket_t &ecat_sync, zmq::socket_t &resource_mgr,
                                   zmq::socket_t &scheduler, zmq::socket_t &ecat_out) {
    int res = 0;
    // Timeout is milliseconds; never pass 0 (busy spin). Events still wake early.
    const long timeout_ms = (poll_wait < 1) ? 1L : static_cast<long>(poll_wait);
    while (!program_done) {
        try {
            long len = 0;
            res = zmq::poll(&items[0], static_cast<size_t>(num_items),
                            std::chrono::milliseconds(timeout_ms));
            if (!res) {
                return res;
            }
#if 0
            for (int i = 0; i < num_items; i++) {
                if (items[i].revents && POLL_IN) {
                    NB_MSG << "Item: " << i << " ";
                }
            }
            NB_MSG << "\n";
#endif
            if (items[internals->ECAT_ITEM].revents & ZMQ_POLLIN) {
                IOLockHelper io_lock;
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
                            assert(msglen == sizeof(global_clock));
                            memcpy(&global_clock, message.data(), msglen);
                            ++stage;
                        }
                        case 2: { // data size
                            zmq::message_t message;
                            // data length
                            ecat_sync.recv(&message);
                            size_t msglen = message.size();
                            DBG_PROCESSING << "recv stage: " << (int)stage << " " << msglen << "\n";
                            assert(msglen == sizeof(incoming_data_size));
                            memcpy(&incoming_data_size, message.data(), msglen);
                            len = incoming_data_size;
                            if (len == 0) {
                                stage = 4;
                                break;
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
                            assert(msglen == incoming_data_size);
                            if (!incoming_process_data) {
                                incoming_process_data = new uint8_t[msglen];
                            }
                            memcpy(incoming_process_data, message.data(), msglen);
#if VERBOSE_DEBUG
                            DBG_PROCESSING << std::flush << "got data: ";
                            display(std::cout, incoming_process_data);
                            DBG_PROCESSING << "\n" << std::flush;
#endif
                            ++stage;
                        }
                        case 4: { // mask
                            zmq::message_t message;
                            ecat_sync.getsockopt(ZMQ_RCVMORE, &more, &more_size);
                            assert(more);
                            ecat_sync.recv(&message);
                            size_t msglen = message.size();
                            DBG_PROCESSING << "recv stage: " << (int)stage << " " << msglen << "\n";
                            assert(msglen == incoming_data_size);
                            if (!incoming_process_mask) {
                                incoming_process_mask = new uint8_t[msglen];
                            }
                            memcpy(incoming_process_mask, message.data(), msglen);
#if VERBOSE_DEBUG
                            std::cout << "got mask: ";
                            display(std::cout, incoming_process_mask);
                            std::cout << "\n";
#endif
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
                            err << "interrupted when sending update (" << (unsigned int)stage
                                << ")";
                            MessageLog::instance()->add(err.str());
                            continue;
                        }
                        else {
                            NB_MSG << "Exception: " << ex.what() << " (" << zmq_strerror(errno)
                                   << ")\n";
                        }
                    }
                }
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

void ProcessingThread::activate(MachineInstance *m) {
    ProcessingThread *pt = instance();
    if (!pt || !m) {
        return;
    }
    boost::recursive_mutex::scoped_lock scoped_lock(pt->runnable_mutex);
    pt->runnable.insert(m);
}

void ProcessingThread::suspend(MachineInstance *m) {
    ProcessingThread *pt = instance();
    if (!pt || !m) {
        return;
    }
    boost::recursive_mutex::scoped_lock scoped_lock(pt->runnable_mutex);
    pt->runnable.erase(m);
}

bool ProcessingThread::is_pending(MachineInstance *m) {
    ProcessingThread *pt = instance();
    if (!pt || !m) {
        return false;
    }
    boost::recursive_mutex::scoped_lock scoped_lock(pt->runnable_mutex);
    return pt->runnable.count(m) != 0;
}

void ProcessingThread::handle_package(Package *p) {
{
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
                ProcessingThread::activate(mi);
                Action *curr = mi->executingCommand();
                if (curr) {
                    DBG_DISPATCHER << mi->getName() << " currently executing "
                                   << *curr << "\n";
                }
            }
        }
    }
    else {
        Dispatcher::instance()->all_receivers.for_each(
            [&](Receiver* r) {
                if (r->receives(m, from)) { r->enqueue(*p); }
        });
    }
}
}

void ProcessingThread::sampleRegularPolls(uint64_t curr_t) {
    static uint64_t last_sample_poll = 0;
    unsigned long sample_us = get_polling_time();
    if (sample_us < 1000) {
        sample_us = 1000;
    }
    if (curr_t - last_sample_poll < sample_us) {
        return;
    }
    last_sample_poll = curr_t;
    if (!machine_is_ready) {
        return;
    }
    // Prefer the live DC application clock (µs) so ANALOG/COUNTER IOTIME keeps
    // advancing even when the ecat thread skips a zero-change domain push.
    uint64_t sample_clock = global_clock;
#ifdef USE_DC
    const uint64_t app_us = ECInterface::instance()->getApplicationTimeUs();
    if (app_us != 0) {
        sample_clock = app_us;
        global_clock = app_us;
    }
#endif
    if (sample_clock == 0) {
        return;
    }
    // Caller must hold IOComponent::lock() (unique_lock is not re-entrant even
    // though the underlying mutex is recursive — double lock aborts).
    // regular_poll only (ANALOGINPUT/COUNTER): filter publishes IOTIME/raw.
    // POINTs stamp IOTIME only when processAll/handleChange sees a bit change.
    handle_io_sampling(sample_clock);
}

void ProcessingThread::HandleIncomingEtherCatData(std::set<IOComponent *> &io_work_queue,
                                                  uint64_t curr_t,
                                                  AutoStatStorage &avg_io_time) {
    IOLockHelper io_lock;
#ifdef KEEPSTATS
    static unsigned long total_mp_time = 0;
    static unsigned long mp_count = 0;
#endif
    uint8_t *mask_p = incoming_process_mask;
    int n = incoming_data_size;
    while (n && *mask_p == 0) {
        ++mask_p;
        --n;
    }
    if (n) { // io has indicated a change
        if (machine_is_ready) {
#if VERBOSE_DEBUG
            std::cout << "Processing got masked EtherCAT data at byte " << (incoming_data_size - n)
                      << "\n";
#endif
#ifdef KEEPSTATS
            AutoStat stats(avg_io_time);
#endif
            IOComponent::processAll(global_clock, incoming_data_size, incoming_process_mask,
                                    incoming_process_data, io_work_queue);
        }
        else {
            std::cout << "Processing received EtherCAT data but machine is not ready\n";
        }
    }
    // Analog/counter sampling (same lock as processAll — do not re-lock).
    sampleRegularPolls(curr_t);
}

ProcessingThread::ProcessingState ProcessingThread::poll_machines() {
#ifdef KEEPSTATS
    avg_clockwork_time.start();
#endif
    std::set<MachineInstance *> to_process;
    {
        boost::recursive_mutex::scoped_lock lock(runnable_mutex);
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
    return eStableStates;
}

void ProcessingThread::handle_mqtt_message(const MQTTInterface::MQTTReceivedMessage &message) {
    if (!message.module) {
        std::stringstream ss;
        ss << "No MQTT module to handle message: " << message.topic;
        MessageLog::instance()->add(ss.str().c_str());
        return;
    }
    MachineInstance *m = message.module->find_handler(message.topic);
    if (!m) {
        std::stringstream ss;
        ss << "No MQTTSUBSCRIBER on module "
            << message.module->getName()
            << " to handle topic: " << message.topic;
        MessageLog::instance()->add(ss.str().c_str());
        return;
    }
    Value received{message.topic, Value::t_string};
    m->setValue("topic", received);
    char *tmp = 0;
    const char *payload = message.value.c_str();
    int64_t val = strtol(payload, &tmp, 10);
    if (tmp && *tmp == 0) {
        m->setValue("message", Value{val});
    }
    else {
        m->setValue("message", Value(payload, Value::t_string));
    }
    const char *evt = payload;
    std::string event("");
    event += evt;
    bool is_enter = false;
    if (strcmp(evt, "on") == 0 || strcmp(evt, "off") == 0) {
        event += "_enter";
        is_enter = true;
    }
    else {
        event = "property_change";
    }
    if (m->_type == "POINT" && (event == "on_enter" || event == "off_enter")) {
        Message msg(event.c_str(), Message::ENTERMSG);
        Package *p = new Package(message.module, m, msg, false);
        Dispatcher::instance()->deliver(p);
    }
    else {
        std::set<MachineInstance *>::iterator iter = m->depends.begin();
        while (iter != m->depends.end()) {
            MachineInstance *mi = *iter++;
            if (!mi->enabled()) { continue; }
            if (mi->_type == "LIST") { continue; }
            Message msg(event.c_str(), (is_enter) ? Message::ENTERMSG : Message::SIMPLEMSG);
            Package *p = new Package(message.module, mi, msg, false);
            Dispatcher::instance()->deliver(p);
        }
    }
}



void ProcessingThread::operator()() {

#ifdef __APPLE__
    pthread_setname_np("iod processing");
#else
    pthread_setname_np(pthread_self(), "iod processing");
#endif

    Statistic *cycle_delay_stat = new Statistic("Cycle Delay");
    Statistic::add(cycle_delay_stat);

    AutoStatStorage avg_io_time("AVG_IO_TIME", 0);
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
    AutoStatStorage avg_update_time("AVG_UPDATE_TIME", 0);
    AutoStatStorage scheduler_delay("SCHEDULER_POLL_SEPARATION", 0);
#endif

    zmq::socket_t sched_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    sched_sync.connect("inproc://scheduler_sync");

    // used to permit command processing
    zmq::socket_t resource_mgr(*MessagingInterface::getContext(), ZMQ_PAIR);
    resource_mgr.connect("inproc://resource_mgr");

    zmq::socket_t ecat_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    ecat_sync.connect("inproc://ethercat_sync");

    zmq::socket_t command_sync(*MessagingInterface::getContext(), ZMQ_PAIR);
    command_sync.connect("inproc://command_sync");

    //IOComponent::setHardwareState(IOComponent::s_hardware_init);
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

    zmq::socket_t ecat_out(*MessagingInterface::getContext(), ZMQ_REQ);
    ecat_out.connect("inproc://ethercat_output");

    checkAndUpdateCycleDelay();

    uint64_t last_checked_cycle_time = 0;
    uint64_t last_checked_plugins = 0;
    uint64_t last_checked_machines = 0;

#ifdef KEEP_STATS
    unsigned long total_cmd_time = 0;
    unsigned long cmd_count = 0;
    unsigned long total_sched_time = 0;
    unsigned long sched_count = 0;
#endif

    uint64_t start_cmd = 0;
    uint64_t last_machine_change = 0;

    MachineInstance *system = MachineInstance::find("SYSTEM");
    assert(system);

    enum { s_update_idle, s_update_sent } update_state = s_update_idle;

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
            if (incoming_process_data) {
                delete incoming_process_data;
                incoming_process_data = 0;
            }
            if (incoming_process_mask) {
                delete incoming_process_mask;
                incoming_process_mask = 0;
            }
            if (activate_hardware.initialiseHardware()) {
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

        // cycle_delay is SYSTEM.POLLING_DELAY (µs). Machine evaluation should
        // not run many times faster than process-data arrives — /5 made the
        // processing thread spin at 5× the poll rate when runnable was non-empty.
        // zmq_poll timeout is milliseconds. Floor at 1 ms so we never busy-spin.
        int poll_wait_ms = static_cast<int>(internals->cycle_delay / 1000);
        if (poll_wait_ms < 1) {
            poll_wait_ms = 1;
        }
        int poll_wait = poll_wait_ms;
        machine_check_delay = static_cast<uint64_t>(
            internals->cycle_delay > 500 ? internals->cycle_delay : 500);
        long systems_waiting = 0;
        uint64_t curr_t = 0;
        bool machines_have_work = false;
        unsigned int num_channels = 0;
        {
            // MQTT subscriber messages
            std::list<MQTTInterface::MQTTReceivedMessage*> to_handle;
            {
                MQTTInterface::MQTTReceivedMessage *message;
                while (mqtt_source_queue.try_dequeue(message)) {
                    to_handle.push_back(message);
                }
            }
            while (!to_handle.empty()) {
                auto message = to_handle.front();
                to_handle.pop_front();
                if (message) {
                    handle_mqtt_message(*message);
                    delete message;
                }
                else {
                    MessageLog::instance()->add("unexpected null message from MQTT");
                }
            }
        }
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
            static uint64_t last_memory_snapshot = 0;
            if (curr_t - program_start >= 300000000 &&
                curr_t - last_memory_snapshot >= 60000000) {
                size_t mail_items = 0;
                size_t active_actions = 0;
                for (auto machine_iter = MachineInstance::begin();
                     machine_iter != MachineInstance::end(); ++machine_iter) {
                    mail_items += (*machine_iter)->mailQueueSize();
                    active_actions += (*machine_iter)->active_actions.size();
                }
                size_t throttled_items = 0;
                const auto *channels = Channel::channels();
                if (channels) {
                    for (const auto &channel : *channels) {
                        throttled_items += channel.second->pendingThrottledCount();
                    }
                }
#if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 33)
                const struct mallinfo2 allocator = mallinfo2();
#else
                // mallinfo2() arrived in glibc 2.33.  Older controller
                // images retain the same fields in mallinfo(), using int
                // counters that are sufficient for this diagnostic snapshot.
                const struct mallinfo allocator = mallinfo();
#endif
                last_memory_snapshot = curr_t;
                std::cerr << "MEMSNAPSHOT"
                          << " scheduler=" << Scheduler::instance()->pendingCount()
                          << " triggers=" << Trigger::liveCount()
                          << " pending_events=" << MachineInstance::pendingEvents().size()
                          << " active_actions=" << active_actions
                          << " mail_items=" << mail_items
                          << " throttled_items=" << throttled_items
                          << " message_log=" << MessageLog::instance()->count()
                          << " cjson_nodes=" << cJSON_LiveNodeCount()
                          << " malloc_in_use_kb=" << allocator.uordblks / 1024
                          << " malloc_free_kb=" << allocator.fordblks / 1024
                          << " malloc_arena_kb=" << allocator.arena / 1024
                          << " malloc_mmap_kb=" << allocator.hblkhd / 1024
                          << " malloc_releasable_kb=" << allocator.keepcost / 1024
                          << "\n";
            }
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

            // Real work = actions/mail/events or IO. A non-empty runnable set of
            // idle machines (stable-check leftovers) is NOT urgent — treating it
            // as such forced ~500 Hz busy polls with nothing to do.
            bool urgent_work = !MachineInstance::pendingEvents().empty() ||
                               IOComponent::updatesWaiting() || !io_work_queue.empty();
            bool stable_pending = false;
            {
                static size_t last_runnable_count = 0;
                boost::recursive_mutex::scoped_lock lock(runnable_mutex);
                size_t runnable_count = runnable.size();
                // Drop inert runnable entries (no exec/mail/events/stable queue).
                // They otherwise accumulate (PROCSNAP runnable=5 with empty exec)
                // and confuse diagnostics without doing work.
                for (auto it = runnable.begin(); it != runnable.end();) {
                    MachineInstance *mi = *it;
                    if (mi->executingCommand() || mi->hasMail() ||
                        !mi->pendingEvents().empty()) {
                        urgent_work = true;
                        ++it;
                        continue;
                    }
                    if (mi->queuedForStableStateTest()) {
                        stable_pending = true;
                        ++it;
                        continue;
                    }
                    it = runnable.erase(it);
                }
                runnable_count = runnable.size();
                // Drive machine eval when there is real work or a stable queue.
                machines_have_work = urgent_work || stable_pending;
                if (runnable_count != last_runnable_count) {
                    last_runnable_count = runnable_count;
                }
            }
            // Refresh time immediately before deciding sleep length.
            curr_t = microsecs();
            // Sleep until the next machine-check window (or ZMQ event).
            {
                int wait_ms = 20; // quiet idle
                if (urgent_work) {
                    wait_ms = poll_wait_ms < 1 ? 1 : poll_wait_ms;
                }
                else if (machines_have_work) {
                    if (curr_t >= last_checked_machines + machine_check_delay) {
                        wait_ms = poll_wait_ms < 1 ? 1 : poll_wait_ms;
                    }
                    else {
                        const uint64_t remain_us =
                            last_checked_machines + machine_check_delay - curr_t;
                        wait_ms = static_cast<int>(remain_us / 1000);
                        if (wait_ms < 1) {
                            wait_ms = 1;
                        }
                        if (wait_ms > 50) {
                            wait_ms = 50;
                        }
                    }
                }
                if (wait_ms < 1) {
                    wait_ms = 1;
                }
                poll_wait = wait_ms;
            }

            //if (Watchdog::anyTriggered(curr_t))
            //  Watchdog::showTriggered(curr_t, true, std::cerr);
            systems_waiting = pollZMQItems(poll_wait, items, 5 + num_channels, ecat_sync,
                                           resource_mgr, sched_sync, ecat_out);
            curr_t = microsecs();

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
#endif

#if 0
        // debug code to work out what machines or systems tend to need processing
        {
            if (systems_waiting > 0 || !io_work_queue.empty() || (machines_have_work || processing_state != eIdle || status != e_waiting)) {
                DBG_PROCESSING << "handling activity. zmq: " << systems_waiting << " state: " << processing_state << " substate: " << status
                        << ((items[internals->ECAT_ITEM].revents & ZMQ_POLLIN) ? " ethercat" : "")
                        << ((IOComponent::updatesWaiting()) ? " io components" : "")
                        << ((!io_work_queue.empty()) ? " io work" : "")
                        << ((machines_have_work) ? " machines" : "")
                        << ((!MachineInstance::pluginMachines().empty() && curr_t - last_checked_plugins >= 1000) ? " plugins" : "")
                        << "\n";
            }
            if (IOComponent::updatesWaiting()) {
                extern std::set<IOComponent *> updatedComponentsOut;
                std::set<IOComponent *>::iterator iter = updatedComponentsOut.begin();
                std::cout << updatedComponentsOut.size() << " entries in updatedComponentsOut:\n";
                while (iter != updatedComponentsOut.end()) {
                    std::cout << " " << (*iter++)->io_name;
                }
                std::cout << " \n";
            }
        }
#endif

        /*  this loop prioritises ethercat processing but if a certain
            number of ethercat cycles have been processed with no
            other activities being given time, we give other jobs
            some time anyway.
        */
        if (items[internals->ECAT_ITEM].revents & ZMQ_POLLIN) {
            HandleIncomingEtherCatData(io_work_queue, curr_t, avg_io_time);
            safeSend(ecat_sync, "go", 2);
            items[internals->ECAT_ITEM].revents = 0;
        }
        else {
            // No domain message this cycle (unchanged image / no push). Still
            // advance ANALOG/COUNTER IOTIME from the live application clock.
            IOLockHelper io_lock;
            sampleRegularPolls(curr_t);
        }

        if (program_done) {
            break;
        }
        if (machine_is_ready && processing_state != eStableStates && !io_work_queue.empty()) {
#ifdef KEEPSTATS
            AutoStat stats(avg_iowork_time);
#endif
            std::set<IOComponent *>::iterator io_work = io_work_queue.begin();
            while (io_work != io_work_queue.end()) {
                IOComponent *ioc = *io_work;
                ioc->handleChange(MachineInstance::pendingEvents());
                io_work = io_work_queue.erase(io_work);
            }
        }

        if (program_done) {
            break;
        }
        if (!MachineInstance::pluginMachines().empty()) {
            // Match POLLING_DELAY (was fixed 1 ms → up to 1 kHz plugin CPU).
            uint64_t plugin_period_us = static_cast<uint64_t>(internals->cycle_delay);
            if (plugin_period_us < 1000) {
                plugin_period_us = 1000;
            }
            if (processing_state == eIdle &&
                curr_t - last_checked_plugins >= plugin_period_us) {
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
            // poll channels
            Channel::handleChannels();
        }

        if (program_done) {
            break;
        }
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
            // Drain only POLLIN sockets using pure ZMQ_DONTWAIT (no safeRecv/poll).
#ifdef KEEPSTATS
            AutoStat stats(avg_cmd_processing);
#endif
            std::list<CommandSocketInfo *>::iterator csi_iter =
                internals->channel_sockets.begin();
            const unsigned int last_i = CommandSocketInfo::lastIndex();
            for (unsigned int i = internals->CMD_SYNC_ITEM; i <= last_i; ++i) {
                zmq::socket_t *sock = nullptr;
                if (i == internals->CMD_SYNC_ITEM) {
                    sock = &command_sync;
                }
                else {
                    if (csi_iter == internals->channel_sockets.end()) {
                        break;
                    }
                    sock = (*csi_iter++)->sock;
                }
                if (i >= static_cast<unsigned int>(max_poll_sockets) ||
                    !(items[i].revents & ZMQ_POLLIN)) {
                    continue;
                }
                for (int nmsg = 0; nmsg < 32; ++nmsg) {
                    MessageHeader mh;
                    const uint32_t default_id = mh.getId();
                    char *buf = nullptr;
                    size_t len = 0;
                    try {
                        zmq::message_t message;
                        // Optional header frame then body (same layout as safeRecv).
                        if (!sock->recv(&message, ZMQ_DONTWAIT)) {
                            break;
                        }
                        if (message.more() && message.size() == sizeof(MessageHeader)) {
                            memcpy(&mh, message.data(), sizeof(MessageHeader));
                            if (!sock->recv(&message, ZMQ_DONTWAIT)) {
                                break;
                            }
                        }
                        len = message.size();
                        buf = new char[len + 1];
                        memcpy(buf, message.data(), len);
                        buf[len] = 0;
                    }
                    catch (const zmq::error_t &) {
                        break;
                    }
                    if (!buf) {
                        break;
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
                        delete command;
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
                        delete[] buf;
                    }
                }
                items[i].revents = 0;
            }
        }

        if (items[internals->SCHEDULER_ITEM].revents & ZMQ_POLLIN) {
#ifdef KEEPSTATS
            if (!scheduler_delay.running()) {
                scheduler_delay.start();
            }
#endif
            if (status == e_waiting && processing_state == eIdle) {
                // DONTWAIT only — safeRecv(...,0) was poll(0) per call.
                size_t len = 0;
                try {
                    len = sched_sync.recv(buf, 10, ZMQ_DONTWAIT);
                }
                catch (const zmq::error_t &) {
                    len = 0;
                }
                if (len) {
                    status = e_handling_sched;
#ifdef KEEPSTATS
                    scheduler_delay.stop();
                    avg_scheduler_time.start();
#endif
                }
            }
            else if (status == e_waiting_sched) {
                size_t len = 0;
                try {
                    len = sched_sync.recv(buf, 10, ZMQ_DONTWAIT);
                }
                catch (const zmq::error_t &) {
                    len = 0;
                }
                if (len) {
                    safeSend(sched_sync, "bye", 3);
                    status = e_waiting;
#ifdef KEEPSTATS
                    avg_scheduler_time.update();
#endif
                }
            }
        }
        if (status == e_handling_sched) {
            size_t len = 0;
            safeSend(sched_sync, "continue", 8);
            status = e_waiting_sched;
        }

        if (machine.activationRequested()) {
            DBG_PROCESSING << " activation requested\n";
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
                        boost::recursive_mutex::scoped_lock lock(runnable_mutex);
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

                    if (!to_process.empty()) {
                        DBG_SCHEDULER << "processing stable states\n";
                        MachineInstance::checkStableStates(to_process, 150000);
                    }
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
                    << " update_state == s_update_idle?: " << (update_state == s_update_idle)
                    << "\n";
        }
        // send a message to the ethercat thread requesting activation
        // or deactivation of the master
        if (status == e_waiting && !IOComponent::devices.empty() && update_state == s_update_idle &&
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
                    update_state = s_update_sent;
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
                 (IOComponent::updatesWaiting() ||
                  IOComponent::getHardwareState() != IOComponent::s_operational)) {
#ifdef KEEPSTATS
            avg_update_time.start();
#endif
            if (update_state == s_update_idle) {
                IOUpdate *upd = 0;
                if (IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
                    DBG_INITIALISATION << "Sending defaults to EtherCAT\n";
                    upd = IOComponent::getDefaults();
                    if (!upd) {
#ifdef USE_KERNEL_ETHERCAT
                        // Seed empty defaults so PROCESS_DATA path can apply
                        // turnOn/turnOff bits into the domain (ecat_thread gates
                        // on default_data for legacy; kernel allows apply anyway).
                        size_t psz = IOComponent::getMaxIOOffset() + 1;
                        if (psz > 0 && psz < 100000) {
                            uint8_t *zdata = new uint8_t[psz];
                            uint8_t *zmask = new uint8_t[psz];
                            memset(zdata, 0, psz);
                            memset(zmask, 0, psz);
                            IOComponent::setDefaultData(zdata);
                            IOComponent::setDefaultMask(zmask);
                            delete[] zdata;
                            delete[] zmask;
                            upd = IOComponent::getDefaults();
                        }
                        if (!upd) {
                            IOComponent::setHardwareState(IOComponent::s_operational);
                            DBG_INITIALISATION
                                << "No process defaults; hardware operational (kernel)\n";
                            continue;
                        }
#else
                        assert(upd);
#endif
                    }
#if VERBOSE_DEBUG
                    if (upd) {
                        display(std::cout, upd->data());
                        std::cout << ":";
                        display(std::cout, upd->mask());
                        std::cout << "\n";
                    }
#endif
                }
                else {
                    upd = IOComponent::getUpdates();
                }
                if (upd) {
                    uint32_t size = upd->size();
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
                                memcpy(iomsg.data(), (void *)upd->data(), size);
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
                                memcpy(iomsg.data(), (void *)upd->mask(), size);
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
                    delete upd;
                    update_state = s_update_sent;
                    IOComponent::updatesSent(true);
#ifdef USE_KERNEL_ETHERCAT
                    // Kernel outputs are applied via the shadow immediately;
                    // drop any leftover pending-out so updatesWaiting() clears.
                    IOComponent::clearPendingOutputUpdates();
#endif
                }
            }
        }
        if (update_state == s_update_sent) {
            char buf[10];
            try {
                if (ecat_out.recv(buf, 10, ZMQ_DONTWAIT)) {
                    update_state = s_update_idle;
                    if (machine.activationRequested()) {
                        if (strncmp(buf, "ok", 2) == 0) {
                            machine.requestActivation(false);
                        }
                    }
                    else if (machine.deactivationRequested()) {
                        if (strncmp(buf, "ok", 2) == 0) {
                            machine.requestDeactivation(false);
                        }
                    }
                    else {
                        if (IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
                            IOComponent::setHardwareState(IOComponent::s_operational);
                        }
                    }
                }
#ifdef KEEPSTATS
                avg_update_time.update();
#endif
            }
            catch (const zmq::error_t &err) {
                if (zmq_errno() != EINTR) {
                    NB_MSG << "Exception: " << err.what() << " (" << zmq_strerror(errno) << ")\n";
                }
                assert(zmq_errno() == EINTR);
            }
        }

        // periodically check to see if the cycle time has been changed
        // more work is needed here since the signaller needs to be told about this
        static int cycle_check_counter = 0;
        if (++cycle_check_counter > 100) {
            cycle_check_counter = 0;
            checkAndUpdateCycleDelay();
        }

        machine.idle(); // in case any of the above triggered a change to the machine state
        last_machine_change = machine.lastUpdated();

        // Cap outer-loop rate. When there is no urgent work, allow a longer
        // quiet period so we are not locked at ~1/POLLING_DELAY busy loops;
        // sampleRegularPolls still advances ANALOG IOTIME on its own timer when
        // EC messages or quiet polls wake us.
        {
            static uint64_t last_iter_us = 0;
            static uint64_t iter_count = 0;
            static uint64_t last_report_us = 0;
            const uint64_t now_us = microsecs();
            uint64_t min_period_us = static_cast<uint64_t>(internals->cycle_delay);
            if (min_period_us < 1000) {
                min_period_us = 1000;
            }
            // Quiet plant: stretch loop toward 5 ms to cut baseline CPU.
            // sampleRegularPolls still runs each wake; EC ZMQ wakes sooner.
            const bool quiet =
                !machines_have_work && io_work_queue.empty() &&
                !IOComponent::updatesWaiting() &&
                MachineInstance::pendingEvents().empty();
            if (quiet && min_period_us < 5000) {
                min_period_us = 5000;
            }
            if (last_iter_us != 0 && now_us - last_iter_us < min_period_us) {
                usleep(static_cast<useconds_t>(min_period_us - (now_us - last_iter_us)));
            }
            last_iter_us = microsecs();
            ++iter_count;
            if (last_report_us == 0) {
                last_report_us = last_iter_us;
            }
            else if (last_iter_us - last_report_us >= 1000000) {
                size_t n_runnable = 0;
                size_t n_stable = 0;
                size_t n_exec = 0;
                size_t n_mail = 0;
                size_t n_events = 0;
                // Sample a few names so we can see what never leaves runnable.
                std::string sample_exec;
                std::string sample_mail;
                std::string sample_stable;
                {
                    boost::recursive_mutex::scoped_lock lock(runnable_mutex);
                    n_runnable = runnable.size();
                    for (MachineInstance *mi : runnable) {
                        const bool st = mi->queuedForStableStateTest();
                        const bool ex = mi->executingCommand() != nullptr;
                        const bool ml = mi->hasMail();
                        if (st) {
                            ++n_stable;
                            if (sample_stable.size() < 140) {
                                if (!sample_stable.empty()) {
                                    sample_stable += ',';
                                }
                                sample_stable += mi->getName();
                            }
                        }
                        if (ex) {
                            ++n_exec;
                            if (sample_exec.size() < 200) {
                                if (!sample_exec.empty()) {
                                    sample_exec += ',';
                                }
                                sample_exec += mi->getName();
                                Action *a = mi->executingCommand();
                                if (a) {
                                    char abuf[80];
                                    a->toString(abuf, sizeof(abuf));
                                    sample_exec += '{';
                                    sample_exec += abuf;
                                    sample_exec += '}';
                                }
                            }
                        }
                        if (ml) {
                            ++n_mail;
                            if (sample_mail.size() < 140) {
                                if (!sample_mail.empty()) {
                                    sample_mail += ',';
                                }
                                sample_mail += mi->getName();
                            }
                        }
                        if (!mi->pendingEvents().empty()) {
                            ++n_events;
                        }
                    }
                }
                std::cerr << "PROCSNAP loops/s=" << iter_count
                          << " cycle_delay_us=" << internals->cycle_delay
                          << " runnable=" << n_runnable
                          << " stableQ=" << n_stable
                          << " exec=" << n_exec
                          << " mail=" << n_mail
                          << " ev=" << n_events
                          << " pendEv=" << MachineInstance::pendingEvents().size()
                          << "\n"
                          << "  exec: " << sample_exec << "\n"
                          << "  mail: " << sample_mail << "\n"
                          << "  stable: " << sample_stable << "\n";
                iter_count = 0;
                last_report_us = last_iter_us;
            }
        }

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
