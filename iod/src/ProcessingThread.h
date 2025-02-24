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

    enum class UpdateStates { s_update_idle, s_update_sent };

    int pollZMQItems(int poll_time, zmq::pollitem_t items[], int num_items,
                     zmq::socket_t &ecat_sync, zmq::socket_t &resource_mgr,
                     zmq::socket_t &sched, zmq::socket_t &ecat_out);

    void waitForCommandProcessing(zmq::socket_t &resource_mgr);
    static uint64_t programStartTime() { return instance()->program_start; }

    void handle_package(Package *p);

    std::set<MachineInstance *>::iterator begin() { return runnable.begin(); }
    std::set<MachineInstance *>::iterator end() { return runnable.end(); }

    void join();

  private:
    static ProcessingThread *instance_;
    ProcessingThread(ControlSystemMachine *m, HardwareActivation &activator,
                     IODCommandThread &cmd_interface, SharedThreadSafeQueue<Package*> &message_queue,
                     SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage *> &mqtt_source_queue);
    ProcessingThread(const ProcessingThread &other);
    ProcessingThread &operator=(const ProcessingThread &other);

    void HandleIncomingEtherCatData(std::set<IOComponent *> &io_work_queue, uint64_t curr_t,
                                    uint64_t last_sample_poll, AutoStatStorage &avg_io_time);

    HardwareActivation &activate_hardware;
    IODCommandThread &command_interface;
    SharedThreadSafeQueue<Package*> &message_queue;
    void handle_mqtt_message(const MQTTInterface::MQTTReceivedMessage &message);
    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> &mqtt_source_queue;
    uint64_t program_start;

    std::set<MachineInstance *> runnable;
};

#endif
