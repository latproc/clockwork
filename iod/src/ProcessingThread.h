#ifndef __cw_processingthread_h__
#define __cw_processingthread_h__

#include "ClientInterface.h"
#include "clockwork.h"

class HardwareActivation {
	public:
		virtual ~HardwareActivation() {}
		virtual void operator()(void) { }
};

class ProcessingThreadInternals;
class ProcessingThread : public ClockworkProcessManager
{
public:
	ProcessingThreadInternals *internals;

    ProcessingThread(ControlSystemMachine &m, HardwareActivation &activator, IODCommandThread &cmd_interface);
	~ProcessingThread();

	void operator()();

	void stop();
    bool checkAndUpdateCycleDelay();
    
    ControlSystemMachine &machine;
    
    enum Status { e_waiting, e_handling_ecat, e_start_handling_commands,
				e_handling_cmd, e_waiting_cmd, e_command_done, 
				e_handling_dispatch,
				e_handling_sched, e_waiting_sched } ;
    Status status;

    int pollZMQItems(int poll_time, 
			zmq::pollitem_t items[], int num_items,
			zmq::socket_t &ecat_sync,
			zmq::socket_t &resource_mgr, zmq::socket_t &dispatch, 
			zmq::socket_t &sched, zmq::socket_t &ecat_out);

    void waitForCommandProcessing(zmq::socket_t &resource_mgr);

private:
    
    ProcessingThread(const ProcessingThread &other);
    ProcessingThread &operator=(const ProcessingThread &other);
	HardwareActivation &activate_hardware;
	IODCommandThread &command_interface;
};

#endif
