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
#ifndef EC_SIMULATOR
#include "ElcSetupRecipe.h"
#endif
#include "ProcessingThread.h"
#include "StallTrace.h"
#include "watchdog.h"
#include <pthread.h>
#include "SharedWorkSet.h"
#include "Dispatcher.h"
#include "Scheduler.h"
#include "Trigger.h"
#include <sstream>

#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace {
bool sched_payload_is(const char *buf, size_t len, const char *word) {
    const size_t n = std::strlen(word);
    return buf && word && len >= n && std::memcmp(buf, word, n) == 0;
}
} // namespace

extern bool program_done;
extern bool machine_is_ready;
extern "C" long cJSON_LiveNodeCount(void);
extern Statistics *statistics;
extern uint64_t client_watchdog_timer;
uint64_t clockwork_watchdog_timer = 0;

extern void handle_io_sampling(uint64_t clock);

//#define KEEPSTATS

#define VERBOSE_DEBUG 0

#ifndef EC_SIMULATOR
/** Install app process mask and mark IO operational without DEFAULT_DATA ZMQ.
 *  Kernel path applies output defaults at activate; optional offline slaves
 *  (e.g. dual-domain servos) must not leave hardware stuck in s_hardware_init
 *  (that forced ~300 brk_out/s with empty out queue). */
static void kernelPromoteIoOperational() {
    uint8_t *pm = IOComponent::getProcessMask();
    const int max_off = IOComponent::getMaxIOOffset();
    if (pm && max_off >= 0) {
        // IOComponent mask covers registered bytes only. Pad to full domain
        // image if domain is larger so collectState can walk domain_size safely.
        size_t mask_len = static_cast<size_t>(max_off) + 1;
        size_t dsz = ECInterface::instance()->copyDomainData(nullptr, 0);
        size_t len = mask_len;
        if (dsz > len) {
            len = dsz;
        }
        std::vector<uint8_t> full_mask(len, 0);
        memcpy(full_mask.data(), pm, mask_len);
        ECInterface::instance()->data.setDataSize(len);
        ECInterface::instance()->data.setMinIOIndex(0);
        ECInterface::instance()->data.setMaxIOIndex(static_cast<unsigned int>(len - 1));
        ECInterface::instance()->data.setAppProcessMask(full_mask.data(), len);
        DBG_INITIALISATION << "Kernel path: installed app process mask len=" << len
                           << " io_mask=" << mask_len << " (no DEFAULT_DATA packet)\n";
    }
    else {
        std::cerr << "WARNING: kernel operational without process mask (pm="
                  << (pm ? "ok" : "null") << " max_off=" << max_off
                  << ") — inputs will not update\n";
    }
    IOComponent::setHardwareState(IOComponent::s_operational);
    DBG_INITIALISATION
        << "Hardware operational (kernel); output defaults applied at activate; "
           "processAll enabled without all-slaves OP\n";
}
#endif

boost::mutex ProcessingThread::proc_snap_mutex_;
ProcessingThread::ProcSnap ProcessingThread::last_proc_snap_;
size_t ProcessingThread::peak_runnable_ = 0;
size_t ProcessingThread::peak_stable_ = 0;
size_t ProcessingThread::peak_exec_ = 0;
size_t ProcessingThread::peak_mail_ = 0;
size_t ProcessingThread::peak_events_ = 0;
size_t ProcessingThread::peak_pend_ev_ = 0;
uint64_t ProcessingThread::loops_with_work_ = 0;

ProcessingThread::ProcSnap ProcessingThread::lastProcSnap() {
    boost::mutex::scoped_lock lock(proc_snap_mutex_);
    return last_proc_snap_;
}

void ProcessingThread::storeProcSnap(const ProcSnap &s) {
    boost::mutex::scoped_lock lock(proc_snap_mutex_);
    last_proc_snap_ = s;
}

void ProcessingThread::noteRunnablePeaks(size_t n_runnable, size_t n_stable, size_t n_exec,
                                         size_t n_mail, size_t n_events, size_t n_pend_ev) {
    boost::mutex::scoped_lock lock(proc_snap_mutex_);
    if (n_runnable > peak_runnable_) {
        peak_runnable_ = n_runnable;
    }
    if (n_stable > peak_stable_) {
        peak_stable_ = n_stable;
    }
    if (n_exec > peak_exec_) {
        peak_exec_ = n_exec;
    }
    if (n_mail > peak_mail_) {
        peak_mail_ = n_mail;
    }
    if (n_events > peak_events_) {
        peak_events_ = n_events;
    }
    if (n_pend_ev > peak_pend_ev_) {
        peak_pend_ev_ = n_pend_ev;
    }
}

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

void ProcessingThread::drainMessageQueue() {
    Package *p = 0;
    while (message_queue.try_dequeue(p)) {
        if (p) {
            handle_package(p);
            delete p;
        }
    }
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
    StallTrace::markStage(StallTrace::StageEcatHandle);
    IOLockHelper io_lock;
#ifdef KEEPSTATS
    static unsigned long total_mp_time = 0;
    static unsigned long mp_count = 0;
#endif
    // Always absorb the coherent domain image when a frame arrives.
    // Do not skip when the ecat update_mask is all-zero (quiet diff): processAll
    // uses the static process map so multi-bit DIGITALVALUE (alarms/statuswords)
    // still leave 0 when the wire already has A.76 / fault bits.
    if (incoming_data_size && incoming_process_data && incoming_process_mask) {
        if (machine_is_ready) {
#if VERBOSE_DEBUG
            std::cout << "Processing EtherCAT process image size " << incoming_data_size << "\n";
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
    StallTrace::markStage(StallTrace::StageOuterHousekeeping);
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

    // Opt-in STALLSNAP observer (DEBUG DEBUG_STALLSNAP on). Safe if never enabled.
    StallTrace::init();

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
    // PROCSNAP wait-loop diagnostics (per-second counters, reset on report)
    uint64_t snap_absorb = 0;
    uint64_t snap_brk_dig = 0;
    uint64_t snap_brk_out = 0;
    uint64_t snap_brk_oth = 0;
    uint64_t snap_brk_exec = 0;
    uint64_t snap_oth_idx[8] = {0, 0, 0, 0, 0, 0, 0, 0};

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
        StallTrace::tickHeartbeat();
        StallTrace::syncEnabledFromDebug();
        StallTrace::markStage(StallTrace::StageOuterHousekeeping);
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

        // IO process path readiness. Full-bus OP aggregation is not required;
        // c_operational() (every configured slave OP). On kernel/elc, optional
        // offline slaves (servo domain) make that false forever while primary
        // domain is live — which blocked processAll and left HW in init (brk_out
        // thrash). Kernel: active master + link is enough; digital edges and
        // bus-good still flow via EC ZMQ + processAll as modules recover.
        {
            const bool io_bus_usable =
                ECInterface::active && ECInterface::master_state.link_up;
            if (io_bus_usable) {
                if (!machine_is_ready) {
                    DBG_INITIALISATION
                        << "machine is becoming ready (kernel: master active + link)\n";
                    machine_is_ready = true;
                }
            }
            else if (machine_is_ready) {
                DBG_INITIALISATION << "machine is no longer ready (kernel: inactive/link down)\n";
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
#ifndef EC_SIMULATOR
            ElcSetupRecipe::pollFromProcessingThread();
            ECInterface::flushDomainClockworkMirrors();
#endif
            // MEMSNAPSHOT: opt-in via DEBUG DEBUG_MEMSNAPSHOT on|off (default off).
            static uint64_t last_memory_snapshot = 0;
            if (LOGS(DebugExtra::instance()->DEBUG_MEMSNAPSHOT) &&
                curr_t - program_start >= 300000000 &&
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
                last_memory_snapshot = curr_t;
                std::cerr << "MEMSNAPSHOT"
                          << " scheduler=" << Scheduler::instance()->pendingCount()
                          << " triggers=" << Trigger::liveCount()
                          << " pending_events=" << MachineInstance::pendingEvents().size()
                          << " active_actions=" << active_actions
                          << " mail_items=" << mail_items
                          << " throttled_items=" << throttled_items
                          << " message_log=" << MessageLog::instance()->count()
                          << " cjson_nodes=" << cJSON_LiveNodeCount();
#if defined(__GLIBC__)
#if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 33)
                // mallinfo2() arrived in glibc 2.33.
                const struct mallinfo2 allocator = mallinfo2();
#else
                // Older controller images retain the same fields in mallinfo(),
                // using int counters that are sufficient for this diagnostic.
                const struct mallinfo allocator = mallinfo();
#endif
                std::cerr << " malloc_in_use_kb=" << allocator.uordblks / 1024
                          << " malloc_free_kb=" << allocator.fordblks / 1024
                          << " malloc_arena_kb=" << allocator.arena / 1024
                          << " malloc_mmap_kb=" << allocator.hblkhd / 1024
                          << " malloc_releasable_kb=" << allocator.keepcost / 1024;
#endif
                std::cerr << "\n";
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

            // Urgency tiers (from mqtt-fix d6312cc2):
            //  - io_urgent: digital/domain events that must not wait
            //  - machine_urgent: mail / machine pending events
            //  - exec_only_waiting: SetStateAction etc. with no mail/events
            //  - stable_pending: TIMER re-queues only
            // updatesWaiting is paced separately (not full-urgent).
            // Waiting SetState alone must not pin busy EC pull forever.
            bool io_urgent =
                !MachineInstance::pendingEvents().empty() || !io_work_queue.empty();
            bool machine_urgent = false;
            bool exec_only_waiting = false;
            bool stable_pending = false;
            {
                static size_t last_runnable_count = 0;
                boost::recursive_mutex::scoped_lock lock(runnable_mutex);
                size_t runnable_count = runnable.size();
                // Peak sample BEFORE purging inert entries (end-of-loop "now"
                // is usually empty after work is drained).
                size_t n_stable = 0, n_exec = 0, n_mail = 0, n_events = 0;
                for (MachineInstance *mi : runnable) {
                    if (mi->queuedForStableStateTest()) {
                        ++n_stable;
                    }
                    if (mi->executingCommand()) {
                        ++n_exec;
                    }
                    if (mi->hasMail()) {
                        ++n_mail;
                    }
                    if (!mi->pendingEvents().empty()) {
                        ++n_events;
                    }
                }
                noteRunnablePeaks(runnable_count, n_stable, n_exec, n_mail, n_events,
                                  MachineInstance::pendingEvents().size());
                // Drop inert runnable entries (no exec/mail/events/stable queue).
                for (auto it = runnable.begin(); it != runnable.end();) {
                    MachineInstance *mi = *it;
                    if (mi->hasMail() || !mi->pendingEvents().empty()) {
                        machine_urgent = true;
                        ++it;
                        continue;
                    }
                    if (mi->executingCommand()) {
                        exec_only_waiting = true;
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
                const bool urgent_work = io_urgent || machine_urgent;
                machines_have_work =
                    urgent_work || exec_only_waiting || stable_pending;
                if (machines_have_work) {
                    boost::mutex::scoped_lock plock(proc_snap_mutex_);
                    ++loops_with_work_;
                }
                if (runnable_count != last_runnable_count) {
                    last_runnable_count = runnable_count;
                }
            }
            const bool urgent_work = io_urgent || machine_urgent;
            curr_t = microsecs();
            // Quiet = no urgent work. Stable/exec-only are "semi-quiet" (paced).
            const bool plant_quiet =
                !urgent_work && !stable_pending && !exec_only_waiting;
            const bool paced_only =
                !urgent_work && (stable_pending || exec_only_waiting);
            // Stable-state / waiting-exec recheck interval (µs).
            // Track SYSTEM.POLLING_DELAY (≈ internals->cycle_delay): 2× poll so
            // POINTSSTARTUP (1 ms) → 2 ms, idle 2 ms poll → 4 ms. Not fixed 2 ms.
            uint64_t stable_check_us =
                static_cast<uint64_t>(internals->cycle_delay > 100
                                          ? internals->cycle_delay
                                          : 100) *
                2;
            if (stable_check_us < 500) {
                stable_check_us = 500;
            }
            if (stable_check_us > 20000) {
                stable_check_us = 20000;
            }
            // Pure exec-wait (e.g. MODULE:ERROR / stuck SetState) must not force
            // a full outer loop every 4 ms. Mail, pending events, and digital
            // EC wakes still break ASAP. TIMER/stable keeps the 2×POLL pace.
            if (exec_only_waiting && !stable_pending && !machine_urgent && !io_urgent) {
                if (stable_check_us < 50000) {
                    stable_check_us = 50000; // 50 ms
                }
            }

            // Analog-only pace for ecat pull_due (LIST/PID/plugins). Digital
            // POINT edges bypass this in ecat_thread (push every bus cycle).
            // Quiet ≥ max(5 ms, 2× POLLING_DELAY); busy = POLLING_DELAY.
            // Waiting SetState must not pin busy forever.
            {
                static bool slow_ec_pull = false;
                const unsigned long busy_pull =
                    static_cast<unsigned long>(internals->cycle_delay > 100
                                                   ? internals->cycle_delay
                                                   : 100);
                unsigned long quiet_pull = busy_pull * 2;
                if (quiet_pull < 5000UL) {
                    quiet_pull = 5000UL;
                }
                if (quiet_pull < busy_pull) {
                    quiet_pull = busy_pull;
                }
                if (!io_urgent && !slow_ec_pull) {
                    set_polling_time(quiet_pull);
                    slow_ec_pull = true;
                }
                else if (io_urgent && slow_ec_pull) {
                    set_polling_time(busy_pull);
                    slow_ec_pull = false;
                }
            }

            {
                int wait_ms = 20; // fully idle
                if (urgent_work) {
                    wait_ms = poll_wait_ms < 1 ? 1 : poll_wait_ms;
                }
                else if (paced_only) {
                    // Pace stable/TIMER and waiting SetState; digital still
                    // wakes early via EC ZMQ.
                    if (curr_t >= last_checked_machines + stable_check_us) {
                        wait_ms = 1;
                    }
                    else {
                        const uint64_t remain_us =
                            last_checked_machines + stable_check_us - curr_t;
                        wait_ms = static_cast<int>(remain_us / 1000);
                        if (wait_ms < 1) {
                            wait_ms = 1;
                        }
                        if (wait_ms > 20) {
                            wait_ms = 20;
                        }
                    }
                }
                if (wait_ms < 1) {
                    wait_ms = 1;
                }
                poll_wait = wait_ms;
            }

            // Plugins while quiet: service in-wait (below), not every 10 ms full
            // outer iteration. Busy: POLLING_DELAY (min 1 ms).
            uint64_t plugin_due_us = static_cast<uint64_t>(internals->cycle_delay);
            if (plugin_due_us < 1000) {
                plugin_due_us = 1000;
            }
            if (plant_quiet && plugin_due_us < 10000) {
                plugin_due_us = 10000; // 10 ms while idle
            }

            //if (Watchdog::anyTriggered(curr_t))
            //  Watchdog::showTriggered(curr_t, true, std::cerr);
            // While a scheduler handshake is open the scheduler thread owns the
            // machines, so the command/channel drain below is deferred. Leaving
            // those sockets armed would return POLLIN on every poll with nobody
            // to consume it — a free-running outer loop until "done" arrives.
            // Mask them until the handshake closes; the traffic is picked up on
            // the next poll, exactly as before, just without the spin.
            {
                const bool sched_handshake_open =
                    (status == e_waiting_sched || status == e_handling_sched);
                for (int i = internals->CMD_SYNC_ITEM;
                     i < 5 + static_cast<int>(num_channels); ++i) {
                    if (sched_handshake_open) {
                        items[i].events = 0;
                    }
                    else {
                        items[i].events = (i == internals->CMD_SYNC_ITEM)
                                              ? ZMQ_POLLIN
                                              : (ZMQ_POLLERR | ZMQ_POLLIN);
                    }
                }
            }

            StallTrace::markStage(StallTrace::StageZmqPoll);
            systems_waiting = pollZMQItems(poll_wait, items, 5 + num_channels, ecat_sync,
                                           resource_mgr, sched_sync, ecat_out);
            curr_t = microsecs();
            StallTrace::markStage(StallTrace::StageOuterHousekeeping);

            // ---- In-wait EC + scheduler service (event-safe, no usleep) ----
            // Drain EC first so digital edges are never delayed by a TIMER poke.
            // Slim sched handshake in-wait (continue, then done/bye on a later
            // poll). Do not sit 10 ms waiting for done.
            const int n_poll = 5 + static_cast<int>(num_channels);
            bool other_non_sched = false;
            bool sched_awake = false;
            for (int i = 0; i < n_poll; ++i) {
                if (i == internals->ECAT_ITEM) {
                    continue;
                }
                if (items[i].revents & (ZMQ_POLLIN | ZMQ_POLLERR)) {
                    // Do not count leftover handshake POLLIN as a new scheduler
                    // wake — that is the 66k/s spin after a half-finished
                    // sched/continue (2G4C-120 1 s overshoot).
                    if (i < 8 &&
                        !(i == internals->SCHEDULER_ITEM &&
                          (status == e_waiting_sched || status == e_handling_sched))) {
                        ++snap_oth_idx[i];
                    }
                    if (i == internals->SCHEDULER_ITEM) {
                        sched_awake = true;
                    }
                    else if (i == internals->CMD_ITEM) {
                        // resource_mgr: client time-sync noise. Drain and ignore
                        // (outer path only logged a warning). Do not force a full
                        // outer loop at hundreds of Hz.
                        char junk[64];
                        try {
                            while (resource_mgr.recv(junk, sizeof(junk), ZMQ_DONTWAIT) > 0) {
                            }
                        }
                        catch (const zmq::error_t &) {
                        }
                        items[i].revents = 0;
                    }
                    else if (i == internals->ECAT_OUT_ITEM) {
                        // Reply pending on ecat_out REQ — outer path handles
                        // multi-step update state. Only break if mid-update.
                        if (update_state != s_update_idle) {
                            other_non_sched = true;
                        }
                        else {
                            items[i].revents = 0;
                        }
                    }
                    else {
                        // command_sync + channel sockets: real client traffic
                        other_non_sched = true;
                    }
                }
            }

            auto has_immediate_machine_work = [&]() -> bool {
                if (!MachineInstance::pendingEvents().empty()) {
                    return true;
                }
                boost::recursive_mutex::scoped_lock lock(runnable_mutex);
                for (MachineInstance *mi : runnable) {
                    if (mi->hasMail() || !mi->pendingEvents().empty()) {
                        return true;
                    }
                }
                return false;
            };
            auto has_paced_machine_work = [&]() -> bool {
                if (exec_only_waiting || stable_pending) {
                    return true;
                }
                boost::recursive_mutex::scoped_lock lock(runnable_mutex);
                for (MachineInstance *mi : runnable) {
                    if (mi->executingCommand() || mi->queuedForStableStateTest()) {
                        return true;
                    }
                }
                return false;
            };

            auto finish_sched_done = [&]() -> bool {
                char sbuf[16];
                size_t len = 0;
                try {
                    len = sched_sync.recv(sbuf, sizeof(sbuf), ZMQ_DONTWAIT);
                }
                catch (const zmq::error_t &) {
                    len = 0;
                }
                if (!len || !sched_payload_is(sbuf, len, "done")) {
                    return false;
                }
                safeSend(sched_sync, "bye", 3);
                status = e_waiting;
                items[internals->SCHEDULER_ITEM].revents = 0;
                ++snap_brk_oth;
                return true;
            };

            // Slim handshake: only sched_sync. Never poll/send ecat_sync here
            // (that abort()ed on 2G4C-120). EC runs after this wait loop.
            // No 10 ms abandon. Port of 34a9afdf.
            auto service_scheduler_in_wait = [&]() -> bool {
                if (status == e_handling_sched) {
                    safeSend(sched_sync, "continue", 8);
                    status = e_waiting_sched;
                    return false;
                }
                if (status == e_waiting_sched) {
                    return finish_sched_done();
                }
                if (!(items[internals->SCHEDULER_ITEM].revents & ZMQ_POLLIN)) {
                    return false;
                }
                if (processing_state != eIdle) {
                    return false;
                }
                char sbuf[16];
                size_t len = 0;
                try {
                    len = sched_sync.recv(sbuf, sizeof(sbuf), ZMQ_DONTWAIT);
                }
                catch (const zmq::error_t &) {
                    len = 0;
                }
                if (!len) {
                    items[internals->SCHEDULER_ITEM].revents = 0;
                    return false;
                }
                if (sched_payload_is(sbuf, len, "done")) {
                    safeSend(sched_sync, "bye", 3);
                    status = e_waiting;
                    items[internals->SCHEDULER_ITEM].revents = 0;
                    ++snap_brk_oth;
                    return true;
                }
                safeSend(sched_sync, "continue", 8);
                status = e_waiting_sched;
                items[internals->SCHEDULER_ITEM].revents = 0;
                return false;
            };

            // Open handshake: try done/bye. Break (do not absorb-continue)
            // so outer EC + command path still run.
            if (status == e_waiting_sched || status == e_handling_sched) {
                service_scheduler_in_wait();
                if (status == e_waiting_sched || status == e_handling_sched) {
                    systems_waiting = 1;
                    break;
                }
            }

            if (items[internals->ECAT_ITEM].revents & ZMQ_POLLIN) {
                HandleIncomingEtherCatData(io_work_queue, curr_t, avg_io_time);
                safeSend(ecat_sync, "go", 2);
                items[internals->ECAT_ITEM].revents = 0;

                if (!io_work_queue.empty() ||
                    !MachineInstance::pendingEvents().empty()) {
                    ++snap_brk_dig;
                    systems_waiting = 1;
                    break;
                }
                if (has_immediate_machine_work()) {
                    ++snap_brk_exec;
                    systems_waiting = 1;
                    break;
                }
                if (has_paced_machine_work() &&
                    curr_t - last_checked_machines >= stable_check_us) {
                    ++snap_brk_exec;
                    systems_waiting = 1;
                    break;
                }
                if (other_non_sched) {
                    systems_waiting = 1;
                    break;
                }
                if (sched_awake) {
                    service_scheduler_in_wait();
                    if (status == e_waiting_sched || status == e_handling_sched) {
                        systems_waiting = 1;
                        break;
                    }
                    if (has_immediate_machine_work() || !io_work_queue.empty() ||
                        !MachineInstance::pendingEvents().empty()) {
                        ++snap_brk_exec;
                        systems_waiting = 1;
                        break;
                    }
                    if (has_paced_machine_work() &&
                        curr_t - last_checked_machines >= stable_check_us) {
                        ++snap_brk_exec;
                        systems_waiting = 1;
                        break;
                    }
                }
                // Outputs: leave wait promptly — do not absorb for 5ms+ while
                // digital/analog outs are pending (softstart SetState hangs).
                // (prod-experimental-mqtt-fix 7e062d0c)
                // Kernel: only real pending outs force brk_out. Stuck
                // s_hardware_init with empty out queue was ~300 outer loops/s.
// pending outs only (elc)
                if (IOComponent::updatesWaiting()
                ) {
                    static uint64_t last_out_service_us = 0;
                    // Service pending outs every bus period (min 1 ms), not 5 ms.
                    unsigned long out_us = get_cycle_time();
                    if (out_us < 1000) {
                        out_us = 1000;
                    }
                    if (last_out_service_us == 0 ||
                        curr_t - last_out_service_us >= out_us) {
                        last_out_service_us = curr_t;
                        ++snap_brk_out;
                        systems_waiting = 1;
                        break;
                    }
                }
                ++snap_absorb;
                systems_waiting = 0;
#ifdef KEEPSTATS
                avg_poll_time.update();
                avg_poll_time.start();
#endif
                continue;
            }

            // Pure scheduler wake (no EC this poll): handshake in-wait.
            if (sched_awake && !other_non_sched) {
                service_scheduler_in_wait();
                if (status == e_waiting_sched || status == e_handling_sched) {
                    systems_waiting = 1;
                    break;
                }
                if (has_immediate_machine_work() || !io_work_queue.empty() ||
                    !MachineInstance::pendingEvents().empty()) {
                    ++snap_brk_exec;
                    systems_waiting = 1;
                    break;
                }
                if (has_paced_machine_work() &&
                    curr_t - last_checked_machines >= stable_check_us) {
                    ++snap_brk_exec;
                    systems_waiting = 1;
                    break;
                }
                systems_waiting = 0;
#ifdef KEEPSTATS
                avg_poll_time.update();
                avg_poll_time.start();
#endif
                continue;
            }

            // Urgent ZMQ (cmd/channel) still exits wait immediately.
            if (other_non_sched) {
                break;
            }
            // Paced-only (stable/exec-wait): leave wait at stable_check_us.
            if (paced_only &&
                curr_t - last_checked_machines >= stable_check_us) {
                break;
            }
            // Immediate machine/IO work.
            if (urgent_work &&
                curr_t - last_checked_machines >= machine_check_delay) {
                break;
            }
            // Outputs: same cadence as bus (min 1 ms). Pending digital/analog
            // outs must not wait behind quiet 5–10 ms absorb.
if (IOComponent::updatesWaiting()
            ) {
                static uint64_t last_out_wait_us = 0;
                unsigned long out_us = get_cycle_time();
                if (out_us < 1000) {
                    out_us = 1000;
                }
                if (last_out_wait_us == 0 || curr_t - last_out_wait_us >= out_us) {
                    last_out_wait_us = curr_t;
                    ++snap_brk_out;
                    break;
                }
            }
            // Quiet: run plugins here so we do not exit wait every plugin_due_us.
            if (plant_quiet && !MachineInstance::pluginMachines().empty() &&
                curr_t - last_checked_plugins >= plugin_due_us) {
#ifdef KEEPSTATS
                AutoStat stats(avg_plugin_time);
#endif
                if (processing_state == eIdle) {
                    StallTrace::markStage(StallTrace::StagePlugins);
                    MachineInstance::checkPluginStates();
                    StallTrace::markStage(StallTrace::StageOuterHousekeeping);
                }
                last_checked_plugins = curr_t;
            }
            else if (!plant_quiet && !MachineInstance::pluginMachines().empty() &&
                     curr_t - last_checked_plugins >= plugin_due_us) {
                break; // busy: plugins with the full processing pass
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
                        << ((!MachineInstance::pluginMachines().empty() && curr_t - last_checked_plugins >=
                             static_cast<uint64_t>(internals->cycle_delay > 1000 ? internals->cycle_delay : 1000))
                                ? " plugins"
                                : "")
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
            StallTrace::markStage(StallTrace::StageChannelsCommands);
            Channel::handleChannels();
            StallTrace::markStage(StallTrace::StageOuterHousekeeping);
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
                    if (sched_payload_is(buf, len, "done")) {
                        // Orphan done from an abandoned in-wait handshake.
                        safeSend(sched_sync, "bye", 3);
                        status = e_waiting;
                    }
                    else {
                        status = e_handling_sched;
#ifdef KEEPSTATS
                        scheduler_delay.stop();
                        avg_scheduler_time.start();
#endif
                    }
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
                if (len && sched_payload_is(buf, len, "done")) {
                    safeSend(sched_sync, "bye", 3);
                    status = e_waiting;
#ifdef KEEPSTATS
                    avg_scheduler_time.update();
#endif
                }
            }
        }
        if (status == e_handling_sched) {
            StallTrace::markStage(StallTrace::StageScheduler);
            size_t len = 0;
            safeSend(sched_sync, "continue", 8);
            status = e_waiting_sched;
            StallTrace::markStage(StallTrace::StageOuterHousekeeping);
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
                    StallTrace::markStage(StallTrace::StagePollMachines);
                    processing_state = poll_machines();
                }
                if (processing_state == eStableStates) {
                    StallTrace::markStage(StallTrace::StageStableStates);
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
                    StallTrace::markStage(StallTrace::StageOuterHousekeeping);
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
        // Track how long ecat_out has been waiting for a REP. If activate is
        // pending and the ecat thread was wedged (e.g. pre-activate SDO hang),
        // we cannot recover the in-flight REQ without restart — but once ecat
        // is healthy again a late "ok"/"nack" still clears state. Log slowly.
        static uint64_t update_sent_since_us = 0;
        if (update_state == s_update_idle) {
            update_sent_since_us = 0;
        }
        else if (update_sent_since_us == 0) {
            update_sent_since_us = nowMicrosecs();
        }
        if (machine.activationRequested()) {
            DBG_MSG << "activation requested, status == e_waiting?: " << (status == e_waiting)
                    << " device list empty?: " << IOComponent::devices.empty()
                    << " update_state == s_update_idle?: " << (update_state == s_update_idle)
                    << "\n";
            if (update_state != s_update_idle && update_sent_since_us != 0) {
                const uint64_t stuck_us = nowMicrosecs() - update_sent_since_us;
                if (stuck_us > 2000000ULL) {
                    static uint64_t last_stuck_log = 0;
                    uint64_t t = nowMicrosecs();
                    if (t - last_stuck_log > 5000000ULL) {
                        last_stuck_log = t;
                        std::cerr << "WARNING: activation pending but ecat_out reply stuck for "
                                  << (stuck_us / 1000ULL)
                                  << " ms (ecat thread blocked? SDO before activate?)\n";
                    }
                }
            }
        }
        // Prefer activate over process-data: never send DEFAULT/PROCESS while
        // activation is requested (would occupy the single REQ slot).
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
                    update_sent_since_us = nowMicrosecs();
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
                 !machine.activationRequested() && !machine.deactivationRequested() &&
                 (IOComponent::updatesWaiting() ||
                  IOComponent::getHardwareState() != IOComponent::s_operational)) {
#ifdef KEEPSTATS
            avg_update_time.start();
#endif
            StallTrace::markStage(StallTrace::StageOutputs);
            if (update_state == s_update_idle) {
                IOUpdate *upd = 0;
                if (IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
#ifndef EC_SIMULATOR
                    // Always promote on kernel: do not require DEFAULT_DATA ZMQ
                    // or all-slaves OP. Defaults already applied at activate;
                    // process mask is required for input collect/processAll.
                    kernelPromoteIoOperational();
                    continue;
#else
                    IOComponent::setHardwareState(IOComponent::s_operational);
                    continue;
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
                    // Kernel outputs are applied via the shadow immediately;
                    // drop any leftover pending-out so updatesWaiting() clears.
                    IOComponent::clearPendingOutputUpdates();
                }
                // Do NOT clearPendingOutputUpdates() when getUpdates() is null:
                // that discarded real digital/analog pending turnOn/setValue and
                // left SetStateAction Running forever (softstart stuck starting).
                // Pending outs stay until processAll matches pending_value or
                // a later getUpdates() succeeds. (prod-experimental-mqtt-fix)
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

        // Once-per-second processing snapshot (always stored for SHOW HEALTH / iosh).
        // Main numbers are high-water marks over the second (peaks); "now" is the
        // post-drain instant and is often all zeros — that is normal when idle.
        // Verbose name samples + stderr only when DEBUG DEBUG_PROCSNAP is on.
        {
            static uint64_t last_iter_us = 0;
            static uint64_t iter_count = 0;
            static uint64_t last_report_us = 0;
            last_iter_us = microsecs();
            ++iter_count;
            if (last_report_us == 0) {
                last_report_us = last_iter_us;
            }
            else if (last_iter_us - last_report_us >= 1000000) {
                const bool want_detail = LOGS(DebugExtra::instance()->DEBUG_PROCSNAP);
                size_t n_runnable = 0;
                size_t n_stable = 0;
                size_t n_exec = 0;
                size_t n_mail = 0;
                size_t n_events = 0;
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
                const size_t n_pend = MachineInstance::pendingEvents().size();
                noteRunnablePeaks(n_runnable, n_stable, n_exec, n_mail, n_events, n_pend);
                StallTrace::publishQueueCounts(
                    static_cast<uint32_t>(n_runnable), static_cast<uint32_t>(n_stable),
                    static_cast<uint32_t>(n_exec), static_cast<uint32_t>(n_mail),
                    static_cast<uint32_t>(n_events), static_cast<uint32_t>(n_pend));

                ProcSnap snap;
                snap.at_us = last_iter_us;
                snap.loops_per_sec = iter_count;
                snap.cycle_delay_us = static_cast<unsigned>(internals->cycle_delay);
                snap.now_runnable = n_runnable;
                snap.now_stable = n_stable;
                snap.now_exec = n_exec;
                snap.now_mail = n_mail;
                snap.now_events = n_events;
                snap.now_pend_ev = n_pend;
                snap.sample_stable = sample_stable;
                snap.sample_exec = sample_exec;
                snap.sample_mail = sample_mail;
                snap.absorb = snap_absorb;
                snap.brk_dig = snap_brk_dig;
                snap.brk_out = snap_brk_out;
                snap.brk_exec = snap_brk_exec;
                snap.brk_oth = snap_brk_oth;
                snap.out_n = IOComponent::updatesWaiting();
                {
                    boost::mutex::scoped_lock plock(proc_snap_mutex_);
                    snap.runnable = peak_runnable_;
                    snap.stable = peak_stable_;
                    snap.exec = peak_exec_;
                    snap.mail = peak_mail_;
                    snap.events = peak_events_;
                    snap.pend_ev = peak_pend_ev_;
                    snap.loops_with_work = loops_with_work_;
                    // Reset peaks for next second.
                    peak_runnable_ = peak_stable_ = peak_exec_ = peak_mail_ = 0;
                    peak_events_ = peak_pend_ev_ = 0;
                    loops_with_work_ = 0;
                }
                snap.valid = true;
                storeProcSnap(snap);

                if (want_detail) {
                    std::cerr << "PROCSNAP loops/s=" << snap.loops_per_sec
                              << " work_loops=" << snap.loops_with_work
                              << " cycle_delay_us=" << snap.cycle_delay_us
                              << " absorb=" << snap_absorb
                              << " brk_dig=" << snap_brk_dig
                              << " brk_out=" << snap_brk_out
                              << " brk_exec=" << snap_brk_exec
                              << " brk_oth=" << snap_brk_oth << "\n"
                              << "  peak: runnable=" << snap.runnable
                              << " stableQ=" << snap.stable
                              << " exec=" << snap.exec
                              << " mail=" << snap.mail
                              << " ev=" << snap.events
                              << " pendEv=" << snap.pend_ev << "\n"
                              << "  now:  runnable=" << snap.now_runnable
                              << " stableQ=" << snap.now_stable
                              << " exec=" << snap.now_exec
                              << " mail=" << snap.now_mail
                              << " ev=" << snap.now_events
                              << " pendEv=" << snap.now_pend_ev
                              << "  (now is post-drain; zeros here are normal when idle)\n"
                              << "  exec: " << sample_exec << "\n"
                              << "  mail: " << sample_mail << "\n"
                              << "  stable: " << sample_stable << "\n";
                }
                snap_absorb = snap_brk_dig = snap_brk_out = snap_brk_exec = snap_brk_oth = 0;
                for (int i = 0; i < 8; ++i) {
                    snap_oth_idx[i] = 0;
                }
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
