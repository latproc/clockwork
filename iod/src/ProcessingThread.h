#ifndef __cw_processingthread_h__
#define __cw_processingthread_h__

#include "AutoStats.h"
#include "ClientInterface.h"
#include "clockwork.h"
#include <boost/thread.hpp>
#include <set>
#include <string>
#include <zmq.hpp>
#include <Message.h>
#include <ThreadSafeQueue.h>
#include "MQTTInterface.h"

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

class ProcessingThread : public ClockworkProcessManager {
  public:
    ProcessingThreadInternals *internals;
    static ProcessingThread &create(ControlSystemMachine *m, HardwareActivation &activator,
                                    IODCommandThread &cmd_interface, SharedThreadSafeQueue<Package*> &queue,
                                    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> &mqtt_source_queue);

    ~ProcessingThread();

    static ProcessingThread *instance();
    static void setProcessingThreadInstance(ProcessingThread *pti);
    CommandSocketInfo *addCommandChannel(Channel *);
    CommandSocketInfo *addCommandChannel(CommandSocketInfo *);

    static void activate(MachineInstance *m);
    static void suspend(MachineInstance *m);
    static bool is_pending(MachineInstance *m);

    enum ProcessingState { eIdle, eStableStates, ePollingMachines };
    ProcessingState poll_machines();

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

    int pollZMQItems(int poll_time, zmq::pollitem_t items[], int num_items,
                     zmq::socket_t &ecat_sync, zmq::socket_t &resource_mgr,
                     zmq::socket_t &sched, zmq::socket_t &ecat_out);

    void waitForCommandProcessing(zmq::socket_t &resource_mgr);
    static uint64_t programStartTime() { return instance()->program_start; }

    void handle_package(Package *p);
    void drainMessageQueue();

    std::set<MachineInstance *>::iterator begin() { return runnable.begin(); }
    std::set<MachineInstance *>::iterator end() { return runnable.end(); }

    void join();

    /** Last once-per-second processing snapshot (always updated; PROCSNAP only prints it).
     *  Queue depths are high-water marks over the second — end-of-loop instant is often 0
     *  after work is drained. */
    struct ProcSnap {
        uint64_t at_us = 0;         // when snapshot was taken
        uint64_t loops_per_sec = 0;
        uint64_t loops_with_work = 0; // iterations that saw urgent/stable work
        unsigned cycle_delay_us = 0;
        // Peak (high-water) over the last second
        size_t runnable = 0;
        size_t stable = 0;
        size_t exec = 0;
        size_t mail = 0;
        size_t events = 0;
        size_t pend_ev = 0;
        // Instant sample at report time (usually quiet)
        size_t now_runnable = 0;
        size_t now_stable = 0;
        size_t now_exec = 0;
        size_t now_mail = 0;
        size_t now_events = 0;
        size_t now_pend_ev = 0;
        std::string sample_stable;
        std::string sample_exec;
        std::string sample_mail;
        // mqtt-fix idle diagnostics (also stored)
        uint64_t absorb = 0;
        uint64_t brk_dig = 0;
        uint64_t brk_out = 0;
        uint64_t brk_exec = 0;
        uint64_t brk_oth = 0;
        size_t out_n = 0;
        bool valid = false;
    };
    static ProcSnap lastProcSnap();
    /** Update high-water marks while already holding runnable_mutex (or from activate). */
    static void noteRunnablePeaks(size_t runnable, size_t stable, size_t exec, size_t mail,
                                  size_t events, size_t pend_ev);

  private:
    static void storeProcSnap(const ProcSnap &s);
    static boost::mutex proc_snap_mutex_;
    static ProcSnap last_proc_snap_;
    // Peaks for the current 1s window (updated under proc_snap_mutex_ or briefly).
    static size_t peak_runnable_;
    static size_t peak_stable_;
    static size_t peak_exec_;
    static size_t peak_mail_;
    static size_t peak_events_;
    static size_t peak_pend_ev_;
    static uint64_t loops_with_work_;
    static ProcessingThread *instance_;
    ProcessingThread(ControlSystemMachine *m, HardwareActivation &activator,
                     IODCommandThread &cmd_interface, SharedThreadSafeQueue<Package*> &message_queue,
                     SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage *> &mqtt_source_queue);
    ProcessingThread(const ProcessingThread &other);
    ProcessingThread &operator=(const ProcessingThread &other);

    void HandleIncomingEtherCatData(std::set<IOComponent *> &io_work_queue, uint64_t curr_t,
                                    AutoStatStorage &avg_io_time);
    /** POLLING_DELAY: regular_poll ANALOG/COUNTER IOTIME without a domain push. */
    void sampleRegularPolls(uint64_t curr_t);

    HardwareActivation &activate_hardware;
    IODCommandThread &command_interface;
    SharedThreadSafeQueue<Package*> &message_queue;
    void handle_mqtt_message(const MQTTInterface::MQTTReceivedMessage &message);
    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> &mqtt_source_queue;
    uint64_t program_start;

    boost::recursive_mutex runnable_mutex;
    std::set<MachineInstance *> runnable;
};

#endif
