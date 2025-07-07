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
#include <assert.h>
#include <stdint.h>
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <zmq.hpp>

#include <boost/thread/mutex.hpp>
#include <list>
#include <set>

#include "ClientInterface.h"
#include "DebugExtra.h"
#include "IOInterface.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "Statistic.h"
#include "Statistics.h"
#include "StatisticHelper.h"
#include "clockwork.h"
#include "options.h"
#include "symboltable.h"
#include "Scheduler.h"
#include "daemon.h"

#include "Channel.h"
#include "ControlSystemMachine.h"
#include "ProcessingThread.h"
#include "watchdog.h"
#include <pthread.h>
#include "SharedWorkSet.h"
#include "Dispatcher.h"
#include "ZmqNotifier.h"
#include <sstream>

#include <iostream>

extern bool program_done;
extern bool machine_is_ready;
extern Statistics *statistics;
extern uint64_t client_watchdog_timer;
uint64_t clockwork_watchdog_timer = 0;
bool ProcessingThread::debug_block_ethercat = false;

extern void handle_io_sampling(uint64_t clock);

//#define KEEPSTATS


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
    static const int QUEUE_NOTIFIER = 5;  // notifier for shared queues

    Watchdog processing_wd;
    ClockworkProcessManager process_manager;
    std::list<CommandSocketInfo *> channel_sockets;
    Daemon *daemon;

    // When messages are sent to shared queues, the queue will notify to this socket viz ZMQ.
    //const std::string shared_queue_notification_endpoint = "inproc://shared_queues";
    //ZmqNotifier notify_zmq;
    //zmq::socket_t queue_notification; // recieves notifications from shared queues

    ProcessingThreadInternals()
        : sequence(0), cycle_delay(1000), processing_wd("Processing Loop Watchdog", 2000)
        //,notify_zmq(*MessagingInterface::getContext(), shared_queue_notification_endpoint),
        //  queue_notification(*MessagingInterface::getContext(), ZMQ_PULL)
    {

        //queue_notification.bind(shared_queue_notification_endpoint);
    }
};

ProcessingThread &ProcessingThread::create(ControlSystemMachine &m, HardwareActivation &activator,
                                           IODCommandThread &cmd_interface, Daemon &daemon,
                                           SharedThreadSafeQueue<Package*> &queue,
                                           SharedThreadSafeQueue<MachineInstance*> &refresh_queue,
                                           SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> &mqtt_source_queue,
                                           SharedThreadSafeQueue<ScheduledItem *> &scheduler_queue) {
    if (!instance_) {
        instance_ = new ProcessingThread(m, activator, cmd_interface, daemon, queue, refresh_queue, mqtt_source_queue, scheduler_queue);
    }
    return *instance_;
}

ProcessingThread & ProcessingThread::create(ControlSystemMachine &m, HardwareActivation &activator,
                                           IODCommandThread &cmd_interface, Daemon &daemon, SharedQueueManager &queue_manager) {
    return create(m, activator, cmd_interface, daemon,
                  queue_manager.get<Package*>("processing"),
                  queue_manager.get<MachineInstance*>("refresh"), queue_manager.get<MQTTInterface::MQTTReceivedMessage*>("mqtt_source"),
                  queue_manager.get<ScheduledItem*>("scheduler"));
}

ProcessingThread::ProcessingThread(ControlSystemMachine &m, HardwareActivation &activator,
                                   IODCommandThread &cmd_interface, Daemon &daemon,
                                   SharedThreadSafeQueue<Package*> &queue,
                                   SharedThreadSafeQueue<MachineInstance*> &refresh_queue,
                                   SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> &mqtt_source_queue,
                                   SharedThreadSafeQueue<ScheduledItem *> &scheduler_queue)
    : internals(0), machine(m), activate_hardware(activator),
      command_interface(cmd_interface), message_queue(queue), refresh_queue(refresh_queue), mqtt_source_queue(mqtt_source_queue),
      scheduler_queue(scheduler_queue), program_start(0) {
    program_start = microsecs();
    internals = new ProcessingThreadInternals();
    internals->daemon = &daemon;
    auto notifier = [this]() { internals->daemon->notify_zmq(); };
    queue.set_notifier(notifier);
    refresh_queue.set_notifier(notifier);
    mqtt_source_queue.set_notifier(notifier);
    scheduler_queue.set_notifier(notifier);
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
                uint32_t data_size = *reinterpret_cast<uint32_t*>(message.data());
                ++stage;
            }
            case 3: { // data
                ecat_sync.getsockopt(ZMQ_RCVMORE, &more, &more_size);
                assert(more);
                zmq::message_t message;
                ecat_sync.recv(&message);
                size_t msglen = message.size();
                update.setData(static_cast<uint8_t*>(message.data()), msglen);
                ++stage;
            }
            case 4: { // mask
                zmq::message_t message;
                ecat_sync.getsockopt(ZMQ_RCVMORE, &more, &more_size);
                assert(more);
                ecat_sync.recv(&message);
                size_t msglen = message.size();
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
                                   zmq::socket_t &scheduler, zmq::socket_t &ecat_out,
                                   zmq::socket_t &queues) {
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
            }
            if (items[internals->CMD_ITEM].revents & ZMQ_POLLIN) {
                int x = 0;
            }
            if (items[internals->CMD_SYNC_ITEM].revents & ZMQ_POLLIN) {
                int x = 0;
            }
            // Throw away queue notifications now that an update has been detected.
            if (items[internals->QUEUE_NOTIFIER].revents & ZMQ_POLLIN) {
                zmq::message_t msg;
                while ( queues.recv(&msg, ZMQ_DONTWAIT) ) { }
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

#if 0
void ProcessingThread::activate(MachineInstance *m) {
    boost::recursive_mutex::scoped_lock scoped_lock(instance()->runnable_mutex);
    instance()->runnable.insert(m);
}

void ProcessingThread::suspend(MachineInstance *m) {
    boost::recursive_mutex::scoped_lock scoped_lock(instance()->runnable_mutex);
    instance()->runnable.erase(m);
}

bool ProcessingThread::is_pending(MachineInstance *m) {
    boost::recursive_mutex::scoped_lock scoped_lock(instance()->runnable_mutex);
    return instance()->runnable.count(m);
}
#endif

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
            mi->getStateMachine()->hasType(ClockworkToken::EXTERNAL)) {
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
#ifdef KEEPSTATS
        AutoStat stats(avg_io_time);
#endif
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

void ProcessingThread::handle_plugin_machines(uint64_t curr_t, uint64_t last_checked_plugins) {
    if (!MachineInstance::pluginMachines().empty()) {
        if (curr_t - last_checked_plugins >= 1000) {
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

    long delta, delta2;

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

        // Removed closed channels and other unloaded machines
        MachineInstance::remove_pending();
        unsigned int machine_check_delay;
        machine.idle(); // ControlSystemMachine poll
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
            {(void *)ecat_sync, 0, ZMQ_POLLIN, 0},     {(void *)resource_mgr, 0, ZMQ_POLLIN, 0},
            {(void *)sched_sync, 0, ZMQ_POLLIN, 0},
            {(void *)ecat_out, 0, ZMQ_POLLIN, 0},      {(void *)command_sync, 0, ZMQ_POLLIN, 0},
            {(void *)internals->daemon->queue_notification, 0, ZMQ_POLLIN, 0} // notifier for shared queues
        };
        const int max_poll_sockets = 25; // imposes a limit on the number of channels
        zmq::pollitem_t items[max_poll_sockets];
        memset((void *)items, 0, max_poll_sockets * sizeof(zmq::pollitem_t));
        int dynamic_poll_start_idx = 6;

        int poll_wait = static_cast<int>(internals->cycle_delay / 1000); // millisecs
        machine_check_delay = static_cast<uint32_t>(internals->cycle_delay / 5);
        long systems_waiting = 0;
        uint64_t curr_t = 0;
        uint64_t last_sample_poll = 0;
        static bool machines_have_work = false;
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
        {
            std::list<ScheduledItem*> to_handle;
            ScheduledItem *item;
            while (scheduler_queue.try_dequeue(item)) {
                assert(item);
                DBG_SCHEDULER << "Processing dequeued " << *item << "\n";
                to_handle.push_back(item);
            }
            DBG_SCHEDULER << "Scheduler processing " << to_handle.size() << " items\n";
            for (auto item : to_handle) {
                if (item->trigger) {
                    if (item->trigger->enabled()) {
                        DBG_SCHEDULER << "Scheduler firing trigger " << item->trigger->getName()<< "\n";
                        item->trigger->fire();
                    }
                }
                else if (item->package) {
                    DBG_SCHEDULER << "Scheduler activating package on "
                                  << item->package->receiver->getName() << "\n";
                    item->package->receiver->handle(*item->package->message,
                                                    item->package->transmitter);
                }
                else if (item->action) {
                    DBG_SCHEDULER << "Scheduler activating pushing action to  "
                                  << item->action->getOwner()->getName() << "\n";
                    item->action->getOwner()->push(item->action);
                }
                else {

                    assert(false);
                }
                delete item;
            }
        }
        curr_t = nowMicrosecs();
        internals->process_manager.SetTime(curr_t);
        {
            MachineInstance *to_refresh;
            std::set<MachineInstance *> marked_for_refresh;
            while (refresh_queue.try_dequeue(to_refresh)) {
                if (!marked_for_refresh.count(to_refresh)) {
                    to_refresh->setNeedsCheck(false); // activate the machine, don't push it back
                    marked_for_refresh.insert(to_refresh);
                }
            }
        }

        for (int i = 0; i < dynamic_poll_start_idx; ++i) {
            items[i] = fixed_items[i];
        }
        wait_for_work(
            items, &machine, dynamic_poll_start_idx, curr_t,
            max_poll_sockets, poll_wait, machines_have_work,
            systems_waiting, runnable_mutex,last_machine_change,
            num_channels, machine_check_delay,
            sched_sync, resource_mgr, ecat_sync, command_sync, ecat_out,
            internals->daemon->queue_notification,
            io_work_queue, last_checked_cycle_time, last_checked_plugins,
            last_checked_machines, last_sample_poll, internals->channel_sockets
        );

#ifdef KEEPSTATS
        avg_poll_time.update();
        avg_poll_time.start();
#endif

        if (items[internals->ECAT_ITEM].revents & ZMQ_POLLIN) {
            HandleIncomingEtherCatData(io_work_queue, curr_t, last_sample_poll, avg_io_time);
        }
        if (program_done) { break; }
        if (machine_is_ready && !io_work_queue.empty()) {
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

        if (program_done) { break; }
        handle_plugin_machines(curr_t, last_checked_plugins);

        {
#ifdef KEEPSTATS
            AutoStat stats(avg_channel_time);
#endif
            Channel::handleChannels();
        }

        if (program_done) { break; }
        handle_command(items,internals->CMD_SYNC_ITEM, dynamic_poll_start_idx, num_channels, command_sync, internals->channel_sockets, internals->cycle_delay);

        if (program_done) { break; }

        if (machine.activationRequested()) {
            DBG_PROCESSING << " activation requested\n"
              << " have devices: " << !IOComponent::devices.empty()
              << " update_status " << static_cast<int>(update_state)
              << "\n";
        }
        // send a message to the ethercat thread requesting activation
        // or deactivation of the master
        if (!IOComponent::devices.empty() && update_state == UpdateStates::s_update_idle &&
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
        else if (machine_is_ready && !IOComponent::devices.empty() &&
                 (IOComponent::updatesWaiting() ||
                  IOComponent::getHardwareState() != IOComponent::s_operational)) {
            handle_hardware(
#ifdef KEEPSTATS
                    avg_update_time,
#endif
                    update_state,
                    ecat_out
                );
        }
        if (update_state == UpdateStates::s_update_sent) {
            char ack[10];
            try {
                if (ecat_out.recv(ack, 10, ZMQ_DONTWAIT)) {
                    update_state = UpdateStates::s_update_idle;
                    if (machine.activationRequested()) {
                        if (strncmp(ack, "ok", 2) == 0) {
                            machine.requestActivation(false);
                        }
                    }
                    else if (machine.deactivationRequested()) {
                        if (strncmp(ack, "ok", 2) == 0) {
                            machine.requestDeactivation(false);
                        }
                    }
                    else {
                        if (IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
                            std::cout << "setting hardware state to operational\n";
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

       handle_machines(last_checked_machines, machine_check_delay, curr_t);

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
    DBG_PROCESSING << "processing done\n";
}
