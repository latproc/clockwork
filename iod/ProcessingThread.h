#ifndef __cw_processingthread_h__
#define __cw_processingthread_h__

class HardwareActivation {
	public:
		virtual ~HardwareActivation() {}
		virtual void operator()(void) { }
};

class ProcessingThread
{
public:
    void operator()();
    ProcessingThread(ControlSystemMachine &m, HardwareActivation &activator);
    void stop();
    bool checkAndUpdateCycleDelay();
    
    ControlSystemMachine &machine;
    int sequence;
    long cycle_delay;
    
    static const int ECAT_ITEM = 0;
    static const int CMD_ITEM = 1;
    static const int DISPATCHER_ITEM = 2;
    static const int SCHEDULER_ITEM = 3;
    static const int ECAT_OUT_ITEM = 4;
    
    enum Status { e_waiting, e_handling_ecat, 
				e_handling_cmd, e_waiting_cmd, e_command_done, 
				e_handling_dispatch,
				e_handling_sched, e_waiting_sched } ;
    Status status;

    int pollZMQItems(int poll_time, 
			zmq::pollitem_t items[], zmq::socket_t &ecat_sync, 
			zmq::socket_t &resource_mgr, zmq::socket_t &dispatch, 
			zmq::socket_t &sched, zmq::socket_t &ecat_out);

    void waitForCommandProcessing(zmq::socket_t &resource_mgr);

private:
    
    ProcessingThread(const ProcessingThread &other);
    ProcessingThread &operator=(const ProcessingThread &other);
	HardwareActivation &activate_hardware;
};

#endif
