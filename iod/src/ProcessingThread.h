#ifndef __cw_processingthread_h__
#define __cw_processingthread_h__

#include "AutoStats.h"
#include "ClientInterface.h"
#include "clockwork.h"
#include <boost/thread.hpp>
#include <set>
#include <zmq.hpp>
#include <Message.h>
#include <ThreadSafeQueue.h>
#include "MQTTInterface.h"
#include "SharedQueueManager.h"

class IOComponent;
class HardwareActivation {
  public:
    virtual ~HardwareActivation() = default;
    virtual bool initialiseHardware() = 0;
    virtual void operator()(void) {}
};

class ProcessingThreadInternals;
class ControlSystemMachine;
class CommandSocketInfo;
class Channel;
class MachineInstance;
struct ScheduledItem;
struct Daemon;

class ProcessingThread : public ClockworkProcessManager {
  public:
    ProcessingThreadInternals *internals;
    static ProcessingThread &create(ControlSystemMachine &m, HardwareActivation &activator,
                                    IODCommandThread &cmd_interface, Daemon &daemon,
                                    SharedThreadSafeQueue<Package*> &queue,
                                    SharedThreadSafeQueue<MachineInstance*> &refresh_queue,
                                    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> &mqtt_source_queue,
                                    SharedThreadSafeQueue<ScheduledItem *> &scheduler_queue);

    static ProcessingThread & create(ControlSystemMachine &m, HardwareActivation &activator,
                                    IODCommandThread &cmd_interface, Daemon &daemon, SharedQueueManager &queue_manager);

    ~ProcessingThread();

    static ProcessingThread *instance();
    static void setProcessingThreadInstance(ProcessingThread *pti);
    CommandSocketInfo *addCommandChannel(Channel *);
    CommandSocketInfo *addCommandChannel(CommandSocketInfo *);

    //static void activate(MachineInstance *m);
    //static void suspend(MachineInstance *m);
    //static bool is_pending(MachineInstance *m);

    void operator()();

    void stop();
    bool checkAndUpdateCycleDelay();

    ControlSystemMachine &machine;

    enum Status {
        e_waiting,
        e_handling_ecat,
        e_start_handling_commands,
        e_handling_cmd,
        e_command_done,
        e_handling_sched,
        e_waiting_sched
    };
    Status status;

    enum class ProcessingStates { eIdle, eStableStates, ePollingMachines };
    enum class UpdateStates { s_update_idle, s_update_sent };

    int pollZMQItems(int poll_time, zmq::pollitem_t items[], int num_items,
                     zmq::socket_t &ecat_sync, zmq::socket_t &resource_mgr,
                     zmq::socket_t &sched, zmq::socket_t &ecat_out,
                     zmq::socket_t &queues);

    bool wait_for_work(
        zmq::pollitem_t items[],
        ControlSystemMachine * machine,
        int & dynamic_poll_start_idx,
        uint64_t & curr_t,
        const int max_poll_sockets,
        int & poll_wait,
        bool & machines_have_work,
        long & systems_waiting,
        boost::recursive_mutex & runnable_mutex,
        uint64_t & last_machine_change,
        unsigned int num_channels,
        unsigned int machine_check_delay,
        zmq::socket_t & sched_sync,
        zmq::socket_t & resource_mgr,
        zmq::socket_t & ecat_sync,
        zmq::socket_t & command_sync,
        zmq::socket_t & ecat_out,
        zmq::socket_t & queue_sync,
        std::set<IOComponent *> & io_work_queue,
        uint64_t & last_checked_cycle_time,
        uint64_t & last_checked_plugins,
        uint64_t & last_checked_machines,
        uint64_t & last_sample_poll,
        const std::list<CommandSocketInfo*> &channels
    );
    void waitForCommandProcessing(zmq::socket_t &resource_mgr);
    static uint64_t programStartTime() { return instance()->program_start; }

    void handle_package(Package *p);

    std::set<MachineInstance *>::iterator begin() { return runnable.begin(); }
    std::set<MachineInstance *>::iterator end() { return runnable.end(); }

    void join();

    static void block_ethercat(bool which) { debug_block_ethercat = which; }
    static bool ethercat_is_blocked() { return debug_block_ethercat; }

  private:
    static ProcessingThread *instance_;
    ProcessingThread(ControlSystemMachine &m, HardwareActivation &activator,
                     IODCommandThread &cmd_interface,
                     Daemon &daemon,
                     SharedThreadSafeQueue<Package*> &message_queue,
                     SharedThreadSafeQueue<MachineInstance*> &refresh_queue,
                     SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage *> &mqtt_source_queue,
                     SharedThreadSafeQueue<ScheduledItem *> &scheduler_queue);
    ProcessingThread(const ProcessingThread &other);
    ProcessingThread &operator=(const ProcessingThread &other);

    void HandleIncomingEtherCatData(std::set<IOComponent *> &io_work_queue, uint64_t curr_t,
                                    uint64_t last_sample_poll, AutoStatStorage &avg_io_time);

    void handle_plugin_machines(ProcessingStates polling_states,
        uint64_t curr_t, uint64_t last_checked_plugins);
    void handle_mqtt_message(const MQTTInterface::MQTTReceivedMessage &message);
    void handle_command(zmq::pollitem_t fixed_items[],
        unsigned int command_channel_index,
        int dynamic_poll_start_idx,
        unsigned int num_channels,
        zmq::socket_t &command_sync,
        const std::list<CommandSocketInfo*> & channels,
        long cycle_delay
    );

    void handle_hardware(
#ifdef KEEPSTATS
        AutoStatStorage &avg_update_time,
#endif
        UpdateStates & s_update_idle,
        zmq::socket_t & ecat_out
    );
    void handle_machines(
        uint64_t & last_checked_machines,
        unsigned int & machine_check_delay,
        ProcessingStates &processing_state,
        uint64_t & curr_t
    );

    HardwareActivation &activate_hardware;
    IODCommandThread &command_interface;
    SharedThreadSafeQueue<Package*> &message_queue;
    SharedThreadSafeQueue<MachineInstance*> &refresh_queue;
    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> &mqtt_source_queue;
    SharedThreadSafeQueue<ScheduledItem *> &scheduler_queue;
    uint64_t program_start;
    // sometimes ethercat is not processed and the watchdog
    // triggers. This is to check if it's the processing
    // thread's fault
    static bool debug_block_ethercat;

    boost::recursive_mutex runnable_mutex;
    std::set<MachineInstance *> runnable;
};

#endif
