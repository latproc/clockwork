#ifndef __cw_processingthread_h__
#define __cw_processingthread_h__

#include <set>
#include <boost/thread/mutex.hpp>
#include "ClientInterface.h"
#include "AutoStats.h"
#include "clockwork.h"

class IOComponent;
class HardwareActivation {
	public:
		virtual ~HardwareActivation() {}
		virtual bool initialiseHardware() { return true;}
		virtual void operator()(void) { }
};

class ProcessingThreadInternals;
class ControlSystemMachine;
class CommandSocketInfo;
class Channel;
class ProcessingThread : public ClockworkProcessManager
{
public:
	ProcessingThreadInternals *internals;
	static ProcessingThread &create(ControlSystemMachine *m, HardwareActivation &activator, IODCommandThread &cmd_interface);

	~ProcessingThread();

	static ProcessingThread *instance();
	static void setProcessingThreadInstance( ProcessingThread* pti);
	CommandSocketInfo *addCommandChannel(Channel *);
	CommandSocketInfo *addCommandChannel(CommandSocketInfo*);

	static void activate(MachineInstance *m);
	static void suspend(MachineInstance *m);
	static bool is_pending(MachineInstance *m);

	void operator()();

	void stop();
	bool checkAndUpdateCycleDelay();

	ControlSystemMachine &machine;

	enum Status { e_waiting, e_handling_ecat, e_start_handling_commands,
		e_handling_cmd, e_command_done, 
		e_handling_dispatch,
		e_handling_sched, e_waiting_sched } ;
	Status status;

	int pollZMQItems(int poll_time, 
			zmq::pollitem_t items[], int num_items,
			zmq::socket_t &ecat_sync,
			zmq::socket_t &resource_mgr, zmq::socket_t &dispatch, 
			zmq::socket_t &sched, zmq::socket_t &ecat_out);

	void waitForCommandProcessing(zmq::socket_t &resource_mgr);
	static uint64_t programStartTime() { return instance()->program_start; }

	std::set<MachineInstance*>::iterator begin() { return runnable.begin(); }
	std::set<MachineInstance*>::iterator end() { return runnable.end(); }

private:
	static ProcessingThread *instance_;
	ProcessingThread(ControlSystemMachine *m, HardwareActivation &activator, IODCommandThread &cmd_interface);
	ProcessingThread(const ProcessingThread &other);
	ProcessingThread &operator=(const ProcessingThread &other);

	void initialiseHardware();
	void pollMachine();

	void HandleIncomingEtherCatData( std::set<IOComponent *> &io_work_queue,
		uint64_t curr_t, uint64_t last_sample_poll, AutoStatStorage &avg_io_time);

	HardwareActivation &activate_hardware;
	IODCommandThread &command_interface;
	uint64_t program_start;

	boost::recursive_mutex runnable_mutex;
	std::set<MachineInstance*> runnable;
};

#endif
