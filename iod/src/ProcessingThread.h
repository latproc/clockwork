#ifndef __cw_processingthread_h__
#define __cw_processingthread_h__

#include "AutoStats.h"
#include "ClientInterface.h"
#include "clockwork.h"
#include <bits/stdint-uintn.h>
#include <boost/thread/recursive_mutex.hpp>
#include <set>
#include <zmq.hpp>

class IOComponent;
class HardwareActivation {
  public:
    virtual ~HardwareActivation() {}
    virtual bool initialiseHardware() { return true; }
    virtual void operator()(void) {}
};

class ProcessingThreadInternals;
class ControlSystemMachine;
class CommandSocketInfo;
class Channel;
class MachineInstance;
class ProcessingThread : public ClockworkProcessManager {
  public:
    ProcessingThreadInternals *internals;
    static ProcessingThread &create(ControlSystemMachine *m, HardwareActivation &activator,
                                    IODCommandThread &cmd_interface);

    ~ProcessingThread();

    static ProcessingThread *instance();
    static void setProcessingThreadInstance(ProcessingThread *pti);
    CommandSocketInfo *addCommandChannel(Channel *);
    CommandSocketInfo *addCommandChannel(CommandSocketInfo *);

    static void activate(MachineInstance *m);
    static void suspend(MachineInstance *m);
    static bool is_pending(MachineInstance *m);

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
        e_handling_dispatch,
        e_handling_sched,
        e_waiting_sched
    };
    Status status;

    enum class ProcessingStates { eIdle, eStableStates, ePollingMachines };
    enum class UpdateStates { s_update_idle, s_update_sent };

    int pollZMQItems(int poll_time, zmq::pollitem_t items[], int num_items,
                     zmq::socket_t &ecat_sync, zmq::socket_t &resource_mgr, zmq::socket_t &dispatch,
                     zmq::socket_t &sched, zmq::socket_t &ecat_out);

    void waitForCommandProcessing(zmq::socket_t &resource_mgr);
    static uint64_t programStartTime() { return instance()->program_start; }

    std::set<MachineInstance *>::iterator begin() { return runnable.begin(); }
    std::set<MachineInstance *>::iterator end() { return runnable.end(); }

  private:
    static ProcessingThread *instance_;
    ProcessingThread(ControlSystemMachine *m, HardwareActivation &activator,
                     IODCommandThread &cmd_interface);
    ProcessingThread(const ProcessingThread &other);
    ProcessingThread &operator=(const ProcessingThread &other);

    void HandleIncomingEtherCatData(std::set<IOComponent *> &io_work_queue, uint64_t curr_t,
                                     uint64_t last_sample_poll, AutoStatStorage &avg_io_time);

    void handle_plugin_machines(ProcessingStates polling_states,
        uint64_t curr_t, uint64_t last_checked_plugins);
    void handle_command(zmq::pollitem_t fixed_items[],
        unsigned int command_channel_index,
        int dynamic_poll_start_idx,
        unsigned int num_channels,
        zmq::socket_t &command_sync,
        const std::list<CommandSocketInfo*> & channels,
        long cycle_delay
    );
    void handle_scheduler(
#ifdef KEEPSTATS
        AutoStatStorage &scheduler_delay,
#endif
        zmq::socket_t &sched_sync,
        char *buf,
        Status &status,
        ProcessingStates &processing_state
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
    void wait_for_work(
    zmq::pollitem_t items[],
    ControlSystemMachine * machine,
    int & dynamic_poll_start_idx,
    uint64_t & curr_t,
    const int max_poll_sockets,
    int & poll_wait,
    bool & machines_have_work,
    int & systems_waiting,
    boost::recursive_mutex & runnable_mutex,
    uint64_t & last_machine_change,
    unsigned int num_channels,
    unsigned int machine_check_delay,
    zmq::socket_t & dispatch_sync,
    zmq::socket_t & sched_sync,
    zmq::socket_t & resource_mgr,
    zmq::socket_t & ecat_sync,
    zmq::socket_t & command_sync,
    zmq::socket_t & ecat_out,
    std::set<IOComponent *> & io_work_queue,
    uint64_t & last_checked_cycle_time,
    uint64_t & last_checked_plugins,
    uint64_t & last_checked_machines,
    uint64_t & last_sample_poll,
    const std::list<CommandSocketInfo*> &channels
    );
    HardwareActivation &activate_hardware;
    IODCommandThread &command_interface;
    uint64_t program_start;

    boost::recursive_mutex runnable_mutex;
    std::set<MachineInstance *> runnable;
};

#endif
