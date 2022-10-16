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

#include <assert.h>
#include <unistd.h>
#include "IOComponent.h"
#include <sstream>
#include <stdio.h>
#include <zmq.hpp>
#include <sys/stat.h>
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <list>
#include <map>
#include <utility>
#include <fstream>
#include <set>
#include "cJSON.h"

#include "MachineInstance.h"
#include "options.h"
#include <stdio.h>
#include "Logger.h"
#include "IODCommand.h"
#include "Dispatcher.h"
#include "Statistic.h"
#include "symboltable.h"
#include "DebugExtra.h"
#include "Scheduler.h"
#include "PredicateAction.h"
#include "ModbusInterface.h"
#include "Statistics.h"
#include "IODCommands.h"
#include <signal.h>
#include "clockwork.h"
#include "ClientInterface.h"
#include "MessageLog.h"
#include "MessagingInterface.h"

#include "ControlSystemMachine.h"
#include "ProcessingThread.h"
#include <pthread.h>
#include "Channel.h"
#include "watchdog.h"

#include <boost/foreach.hpp>

extern bool program_done;
extern bool machine_is_ready;
extern Statistics *statistics;
extern uint64_t client_watchdog_timer;
uint64_t clockwork_watchdog_timer = 0;

extern void handle_io_sampling(uint64_t clock);

//#define KEEPSTATS

#define VERBOSE_DEBUG 0
#define MEMCHECK()
//static void MEMCHECK() { char *x = new char[12358]; memset(x,0,12358); delete[] x; }

unsigned int CommandSocketInfo::last_idx = 5;

class ProcessingThreadInternals {
public:
	int sequence;
	long cycle_delay;

	static const int ECAT_ITEM = 0; // ethercat data incoming
	static const int CMD_ITEM = 1;  // client interface time sync
	static const int DISPATCHER_ITEM = 2; // messages being dispatched
	static const int SCHEDULER_ITEM = 3; // scheduled items firing
	static const int ECAT_OUT_ITEM = 4; //io has data update for ethercat
	static const int CMD_SYNC_ITEM = 5; // client interface sending message

	Watchdog processing_wd;
	ClockworkProcessManager process_manager;
	std::list<CommandSocketInfo*> channel_sockets;

	ProcessingThreadInternals() : sequence(0), cycle_delay(1000),
		processing_wd("Processing Loop Watchdog", 2000) { }
};

ProcessingThread &ProcessingThread::create(ControlSystemMachine *m, HardwareActivation &activator, IODCommandThread &cmd_interface) {
	if (!instance_) instance_ = new ProcessingThread(m, activator,cmd_interface);
	return *instance_;
}

ProcessingThread::ProcessingThread(ControlSystemMachine *m, HardwareActivation &activator, IODCommandThread &cmd_interface)
: internals(0), machine(*m), status(e_waiting),
activate_hardware(activator), command_interface(cmd_interface), program_start(0)
{
	program_start = microsecs();
	internals = new ProcessingThreadInternals();
}

ProcessingThread::~ProcessingThread() {
	delete internals;
}

void ProcessingThread::stop()
{
	program_done = true;
	MessagingInterface::abort();
	exit(0);
}

CommandSocketInfo *ProcessingThread::addCommandChannel(CommandSocketInfo *csi) {
	std::list<CommandSocketInfo*>::iterator iter= internals->channel_sockets.begin();
	int idx = 0;
	while (iter != internals->channel_sockets.end() ) {
		CommandSocketInfo *info = *iter++; ++idx;
		if (info == csi) {
			NB_MSG << "Processing thread already has command socket info for " << csi->address << " at index " << idx << "\n";
			return csi; // already configured
		}
	}
	internals->channel_sockets.push_back(csi);
	return csi;
}

CommandSocketInfo *ProcessingThread::addCommandChannel(Channel *chn) {
	if (chn->definition()->isPublisher()) return 0;
	CommandSocketInfo *info = new CommandSocketInfo(chn);
	internals->channel_sockets.push_back(info);
	return info;
}

bool ProcessingThread::checkAndUpdateCycleDelay()
{
	const Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
	long delay = 100;
	if (cycle_delay_v && cycle_delay_v->iValue >= 100)
		delay = cycle_delay_v->iValue;
	if (delay != internals->cycle_delay)
	{
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

static uint8_t *incoming_process_data = 0;
static uint8_t *incoming_process_mask = 0;
static uint32_t incoming_data_size;
static uint64_t global_clock = 0;

#if VERBOSE_DEBUG
static void display(uint8_t *p) {
	int max = IOComponent::getMaxIOOffset();
	int min = IOComponent::getMinIOOffset();
	for (int i=min; i<=max; ++i)
		std::cout << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i] << std::dec;
}
#endif

class IOLockHelper {
public:
	IOLockHelper() { IOComponent::lock(); }
	~IOLockHelper() { IOComponent::unlock(); }
};


int ProcessingThread::pollZMQItems(int poll_wait, zmq::pollitem_t items[], int num_items,
		zmq::socket_t &ecat_sync,
		zmq::socket_t &resource_mgr,
		zmq::socket_t &dispatcher,
		zmq::socket_t &scheduler,
		zmq::socket_t &ecat_out)
{
	int res = 0;
	while (!program_done )
	{
		MEMCHECK();
		try
		{
			long len = 0;
			char buf[10];
			res = zmq::poll(&items[0], num_items, poll_wait);
			if (!res) return res;
#if 0
			for (int i=0; i<num_items; i++) {
				if (items[i].revents && POLL_IN) {
					NB_MSG << "Item: " <<i << " ";
				}
			}
			NB_MSG << "\n";
#endif
			if (items[internals->ECAT_ITEM].revents & ZMQ_POLLIN)
			{
				IOLockHelper io_lock;
				// the EtherCAT message carries a mask and data

				int64_t more;
				size_t more_size = sizeof (more);
				uint8_t stage = 1;
				//  Process all parts of the message
				while (true) {
					MEMCHECK();
					try {
						switch (stage) {
							case 1: // global clock
								{
									zmq::message_t message;
									// clock
									ecat_sync.recv(&message);
									size_t msglen = message.size();
#if VERBOSE_DEBUGx
DBG_MSG << "recv stage: " << (int)stage << " " << msglen << "\n";
#endif
									assert(msglen == sizeof(global_clock));
									memcpy(&global_clock, message.data(), msglen);
									++stage;
								}
							case 2: // data size
								{
									zmq::message_t message;
									// data length
									ecat_sync.recv(&message);
									size_t msglen = message.size();
#if VERBOSE_DEBUGx
DBG_MSG << "recv stage: " << (int)stage << " " << msglen << "\n";
#endif
									assert(msglen == sizeof(incoming_data_size));
									memcpy(&incoming_data_size, message.data(), msglen);
									len = incoming_data_size;
									if (len == 0) { stage = 4; break; }
									++stage;
								}
							case 3: // data
								{
									ecat_sync.getsockopt( ZMQ_RCVMORE, &more, &more_size);
									assert(more);
									zmq::message_t message;
									ecat_sync.recv(&message);
									size_t msglen = message.size();
#if VERBOSE_DEBUGx
DBG_MSG << "recv stage: " << (int)stage << " " << msglen << "\n";
#endif
									assert(msglen == incoming_data_size);
									if (!incoming_process_data) incoming_process_data = new uint8_t[msglen];
									memcpy(incoming_process_data, message.data(), msglen);
#if VERBOSE_DEBUG
									std::cout << std::flush << "got data: ";
									display(incoming_process_data); std::cout << "\n" << std::flush;
#endif
									++stage;
								}
							case 4: // mask
								{
									zmq::message_t message;
									ecat_sync.getsockopt( ZMQ_RCVMORE, &more, &more_size);
									assert(more);
									ecat_sync.recv(&message);
									size_t msglen = message.size();
#if VERBOSE_DEBUGx
									DBG_MSG << "recv stage: " << (int)stage << " " << msglen << "\n";
#endif
									assert(msglen == incoming_data_size);
									if (!incoming_process_mask) incoming_process_mask = new uint8_t[msglen];
									memcpy(incoming_process_mask, message.data(), msglen);
#if VERBOSE_DEBUG
									std::cout << "got mask: "; display(incoming_process_mask); std::cout << "\n";
#endif
									++stage;
									break;
								}
							default: {
								DBG_MSG << "unexpected stage " << (int)stage << "\n";
								assert(stage<=4);
								};
						}
						break;
					}
					catch(const zmq::error_t &ex) {
						if (zmq_errno() == EINTR) {
							NB_MSG << "interrupted when sending update (" << (unsigned int)stage << ")\n";
							continue;
						}
						else {
							NB_MSG << "Exception: " << ex.what() << " (" << zmq_strerror(errno) << ")\n";
						}

					}
				}
				break;
			}
			break;
		}
		catch (const std::exception &ex)
		{
			if (errno == EINTR) continue; // TBD watch for infinite loop here
			const char *fnam = strrchr(__FILE__, '/');
			if (!fnam) fnam = __FILE__; else fnam++;
			NB_MSG << "Error " << ex.what() << " (" << zmq_strerror(errno)
				<< ") in " << fnam << ":" << __LINE__ << "\n";
			break;
		}
	}
	return res;
}

ProcessingThread *ProcessingThread::instance_ = 0;
ProcessingThread *ProcessingThread::instance() {
	return instance_;
}
void ProcessingThread::setProcessingThreadInstance( ProcessingThread* pti) {
	instance_ = pti;
}

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


void ProcessingThread::HandleIncomingEtherCatData( std::set<IOComponent *> &io_work_queue,
		uint64_t curr_t, uint64_t last_sample_poll, AutoStatStorage &avg_io_time) {
	IOLockHelper io_lock;
	static unsigned long total_mp_time = 0;
	static unsigned long mp_count = 0;
	uint8_t *mask_p = incoming_process_mask;
	int n = incoming_data_size;
	while (n && *mask_p == 0) { ++mask_p; --n; }
	if (n) { // io has indicated a change
		if (machine_is_ready)
		{
#if VERBOSE_DEBUG
			std::cout << "Processing got masked EtherCAT data at byte " << (incoming_data_size-n) << "\n";
#endif
#ifdef KEEPSTATS
			AutoStat stats(avg_io_time);
#endif
			IOComponent::processAll( global_clock, incoming_data_size, incoming_process_mask,
					incoming_process_data, io_work_queue);
		}
		else
			std::cout << "Processing received EtherCAT data but machine is not ready\n";

	}
	if (curr_t - last_sample_poll >= 10000) {
		last_sample_poll = curr_t;
		handle_io_sampling(global_clock); // devices that need a regular poll
	}
}

void ProcessingThread::operator()()
{

#ifdef __APPLE__
	pthread_setname_np("iod processing");
#else
	pthread_setname_np(pthread_self(), "iod processing");
#endif


	Statistic *cycle_delay_stat = new Statistic("Cycle Delay");
	Statistic::add(cycle_delay_stat);
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

	zmq::socket_t dispatch_sync(*MessagingInterface::getContext(), ZMQ_REQ);
	dispatch_sync.connect("inproc://dispatcher_sync");

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

	safeSend(sched_sync,"go",2); // scheduled items
	usleep(10000);
	safeSend(dispatch_sync,"go",2); //  permit handling of events
	usleep(10000);

	std::cout << "----------- Enabling client access --------\n";
	FileLogger fl(program_name); fl.f() << "Enabling client access\n";
	safeSend(resource_mgr, "start", 5);

	safeSend(ecat_sync,"go",2); // collect state
	usleep(10000);

	zmq::socket_t ecat_out(*MessagingInterface::getContext(), ZMQ_REQ);
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

	enum { s_update_idle, s_update_sent } update_state = s_update_idle;

	bool commands_started = false;

	enum { eIdle, eStableStates, ePollingMachines} processing_state = eIdle;
	std::set<IOComponent *>io_work_queue;

	//  we need to stop polling io (ie exit) if the control threads do not seem
	// to be responding.
	const int MAX_UNCONTROLLED_POLLS = 5;
	int io_unsafe_polls_remaining = MAX_UNCONTROLLED_POLLS;
	while (!program_done)
	{
		MEMCHECK();
		if (IOComponent::getHardwareState() == IOComponent::s_hardware_preinit) {
			IOLockHelper io_lock;
			// attempt to initialise the hardware interface. If this
			// works we move the IOComponent module's state along
			// so that IOComponents can be linked
			if (incoming_process_data) { delete incoming_process_data; incoming_process_data = 0; }
			if (incoming_process_mask) { delete incoming_process_mask; incoming_process_mask = 0; }
			if (activate_hardware.initialiseHardware()) {
				IOComponent::setHardwareState(IOComponent::s_hardware_init);
				//std::cout << "Activating hardware\n";
				//activate_hardware();
			}
		}

		unsigned int machine_check_delay;
		machine.idle();
		last_machine_change = machine.lastUpdated();

		// only process io components if the machine is operational
		if (machine.c_operational()) {
			if (!machine_is_ready) {
				std::cout << "machine is becoming ready\n";
				machine_is_ready = true;
			}
		}
		else {
			if (machine_is_ready) {
				std::cout << "machine is no longer ready\n";
				machine_is_ready = false;
			}
		}


#ifdef KEEPSTATS
		avg_poll_time.start();
#endif

		zmq::pollitem_t fixed_items[] =
		{
			{ (void*)ecat_sync, 0, ZMQ_POLLIN, 0 },
			{ (void*)resource_mgr, 0, ZMQ_POLLIN, 0 },
			{ (void*)dispatch_sync, 0, ZMQ_POLLIN, 0 },
			{ (void*)sched_sync, 0, ZMQ_POLLIN, 0 },
			{ (void*)ecat_out, 0, ZMQ_POLLIN, 0 },
			{ (void*)command_sync, 0, ZMQ_POLLIN, 0 }
		};
		const int max_poll_sockets = 25;
		zmq::pollitem_t items[max_poll_sockets];
		memset((void*)items, 0, max_poll_sockets * sizeof(zmq::pollitem_t));
		int dynamic_poll_start_idx = 6;

		char buf[100];
		int poll_wait = internals->cycle_delay / 1000; // millisecs
		machine_check_delay = internals->cycle_delay / 5;
		int systems_waiting = 0;
		uint64_t curr_t = 0;
		uint64_t last_sample_poll = 0;
		bool machines_have_work = false;
		unsigned int num_channels = 0;
		while (!program_done)
		{
			MEMCHECK();
			curr_t = nowMicrosecs();
			internals->process_manager.SetTime(curr_t);
			//TBD add a guard here to detect/prevent rapid cycling

			for (int i=0; i<dynamic_poll_start_idx; ++i) {
				items[i] = fixed_items[i];
			}

			// add the channel sockets to our poll info
			{
				std::list<CommandSocketInfo*>::iterator csi_iter = internals->channel_sockets.begin();
				int idx = dynamic_poll_start_idx;
				while (csi_iter != internals->channel_sockets.end()) {
					CommandSocketInfo *info = *csi_iter++;
					items[idx].socket = (void*)(*info->sock);
					items[idx].fd = 0;
					items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;
					items[idx].revents = 0;
					idx++;
					if (idx == max_poll_sockets) break;
				}
				num_channels = idx - dynamic_poll_start_idx; // the number channels we are actually monitoring
			}

			//machines_have_work = MachineInstance::workToDo();
			{
				static size_t last_runnable_count = 0;
				boost::recursive_mutex::scoped_lock lock(runnable_mutex);
				machines_have_work = !runnable.empty() || !MachineInstance::pendingEvents().empty();
				size_t runnable_count = runnable.size();
				if (runnable_count != last_runnable_count) {
					//DBG_MSG << "runnable: " << runnable_count << " (was " << last_runnable_count << ")\n";
					last_runnable_count = runnable_count;
				}
			}
			if (machines_have_work || IOComponent::updatesWaiting() || !io_work_queue.empty())
				poll_wait = 1;
			else {
				poll_wait = 100;
			}

			//if (Watchdog::anyTriggered(curr_t))
			//	Watchdog::showTriggered(curr_t, true);
			systems_waiting = pollZMQItems(poll_wait, items, 6 + num_channels,
				ecat_sync, resource_mgr, dispatch_sync, sched_sync, ecat_out);

			if (systems_waiting > 0
				|| (machines_have_work && curr_t - last_checked_machines >= machine_check_delay)) break;
			if (IOComponent::updatesWaiting() || !io_work_queue.empty()) break;
			if (!MachineInstance::pluginMachines().empty() && curr_t - last_checked_plugins >= 1000) break;
			if ( curr_t - last_machine_change >10000) { last_machine_change = curr_t; machine.idle(); }
			if ( last_machine_change < machine.lastUpdated() ) break;
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
			if (systems_waiting > 0 || !io_work_queue.empty() || (machines_have_work || processing_state != eIdle || status != e_waiting) ) {
				DBG_MSG << "handling activity. zmq: " << systems_waiting << " state: " << processing_state << " substate: " << status
					<< ( (items[internals->ECAT_ITEM].revents & ZMQ_POLLIN) ? " ethercat" : "")
					<< ( (IOComponent::updatesWaiting()) ? " io components" : "")
					<< ( (!io_work_queue.empty()) ? " io work" : "")
					<< ( (machines_have_work) ? " machines" : "")
					<< ( (!MachineInstance::pluginMachines().empty() && curr_t - last_checked_plugins >= 1000) ? " plugins" : "")
					<< "\n";
			}
			if (IOComponent::updatesWaiting()) {
				extern std::set<IOComponent*> updatedComponentsOut;
				std::set<IOComponent*>::iterator iter = updatedComponentsOut.begin();
				std::cout << updatedComponentsOut.size() << " entries in updatedComponentsOut:\n";
				while (iter != updatedComponentsOut.end()) std::cout << " " << (*iter++)->io_name;
				std::cout << " \n";
			}
		}
#endif

		/* this loop prioritises ethercat processing but if a certain
			number of ethercat cycles have been processed with no
			other activities being given time, we give other jobs
			some time anyway.
		*/
		if (items[internals->ECAT_ITEM].revents & ZMQ_POLLIN)
		{
			HandleIncomingEtherCatData(io_work_queue, curr_t, last_sample_poll, avg_io_time);
			safeSend(ecat_sync,"go",2);
		}

		if (program_done) break;
		if  (machine_is_ready && processing_state != eStableStates &&  !io_work_queue.empty()) {
			//DBG_MSG << " processing io changes\n";
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

		if (program_done) break;
		if (!MachineInstance::pluginMachines().empty()) {
			if (processing_state == eIdle  && curr_t - last_checked_plugins >= 1000) {
#ifdef KEEPSTATS
				AutoStat stats(avg_plugin_time);
#endif
				MachineInstance::checkPluginStates();
				last_checked_plugins = curr_t;
			}
		}
		else last_checked_plugins = curr_t;

		if (status == e_waiting) {
#ifdef KEEPSTATS
			AutoStat stats(avg_channel_time);
#endif
			// poll channels
			Channel::handleChannels();
		}

		if (program_done) break;
		if (status == e_waiting)  {
			if (items[internals->CMD_ITEM].revents & ZMQ_POLLIN) {
#ifdef KEEPSTATS
				AutoStat stats(avg_cmd_processing);
#endif
				size_t len = resource_mgr.recv(buf, 100, ZMQ_NOBLOCK);
				if (len) {
					char msgbuf[200];
					snprintf(msgbuf, 200, "Warning: processing thread ignoring incoming data '%.*s' from client\n", (int)len, buf);
					MessageLog::instance()->add(msgbuf);
				}
			}
		}

		if (program_done) break;

		if (status == e_waiting && items[internals->DISPATCHER_ITEM].revents & ZMQ_POLLIN) {
			if (status == e_waiting)
			{
				size_t len = dispatch_sync.recv(buf, 10, ZMQ_NOBLOCK);
				if (len) {
					status = e_handling_dispatch;
				}
			}
		}
		if (status == e_handling_dispatch) {
			//DBG_MSG << " processing dispatcher\n";
			if (processing_state != eIdle) {
				// cannot process dispatch events at present
				status = e_waiting;
			}
			else {
#ifdef KEEPSTATS
				AutoStat stats(avg_dispatch_time);
#endif
				size_t len = 0;
				safeSend(dispatch_sync,"continue",3);
				// wait for the dispatcher
				safeRecv(dispatch_sync, buf, 10, true, len, 0);
				safeSend(dispatch_sync,"bye",3);
				status = e_waiting;
			}
		}

		if (status == e_waiting && systems_waiting > 0) {
			// check the command interface and any command channels for activity
			bool have_command = false;
			if (items[internals->CMD_SYNC_ITEM].revents & ZMQ_POLLIN) {
				//NB_MSG << "Processing thread has a command from the client interface\n";
				have_command = true;
			}
			else {
				for (unsigned int i = dynamic_poll_start_idx; i < dynamic_poll_start_idx + num_channels; ++i) {
					if (items[i].revents & ZMQ_POLLIN) {
						//NB_MSG << "Processing thread has a command from a channel command interface\n";
						have_command = true;
						break;
					}
				}
			}
			if ( have_command) {
				//NB_MSG << "processing incoming commands\n";
				uint64_t start_time = microsecs();
				uint64_t now = start_time;
#ifdef KEEPSTATS
				AutoStat stats(avg_cmd_processing);
#endif
				int count = 0;
				while (have_command && (long)(now - start_time) < internals->cycle_delay/2) {
					have_command = false;
					std::list<CommandSocketInfo*>::iterator csi_iter = internals->channel_sockets.begin();
					unsigned int i = internals->CMD_SYNC_ITEM;
					while (i<=CommandSocketInfo::lastIndex() && (long)(now - start_time) < internals->cycle_delay/2 ) {
						zmq::socket_t *sock = 0;
						CommandSocketInfo *info = 0;
						if (i == internals->CMD_SYNC_ITEM) {
							sock = &command_sync;
						}
						else {
							if (csi_iter == internals->channel_sockets.end()) break;
							info = *csi_iter++;
							sock = info->sock;
						}
						{
						int rc = zmq::poll(&items[i], 1, 0);
						}
						if (! (items[i].revents & ZMQ_POLLIN) ) {
							++i;
							continue;
						}
#if 0
						if (i>5) {
							NB_MSG << "Processing thread has activity on poll item " << i << " of 0.." << CommandSocketInfo::lastIndex() << "\n";
						}
#endif
						have_command = true;

						zmq::message_t msg;
						char *buf = 0;
						size_t len = 0;
						MessageHeader mh;
						uint32_t default_id = mh.getId(); // save the msgid to following check
						if (safeRecv(*sock, &buf, &len, false, 0, mh) ) {
							++count;
							if (false && len>10){
								FileLogger fl(program_name);
								fl.f() << "Processing thread received command ";
								if (buf)fl.f() << buf << " "; else fl.f() << "NULL";
								fl.f() << "\n";
							}
							if (!buf) continue;
							IODCommand *command = parseCommandString(buf);
							if (command) {
								bool ok = false;
								try {
									//NB_MSG << "processing thread executing " << buf << "\n";
									ok  = (*command)();
									//NB_MSG << "execution result " << command->result() << "\n";
								}
								catch (const std::exception & e) {
									FileLogger fl(program_name);
									fl.f() << "command execution threw an exception " << e.what() << "\n";
								}
								delete[] buf;

								if (mh.needsReply() || mh.getId() == default_id) {
									char *response = strdup( (ok) ? command->result() : command->error());
									MessageHeader rh(mh);
									rh.source = mh.dest;
									rh.dest = mh.source;
									rh.start_time = microsecs();
									safeSend(*sock, response, strlen(response), rh);
									free(response);
								}
								else {
									//char *response = strdup(command->result());
									//safeSend(*sock, response, strlen(response));
									//free(response);
								}
							}
							else {
								if (mh.needsReply() || mh.getId() == default_id) {
									char *response = new char[len+40];
									snprintf(response, len+40, "Unrecognised command: %s", buf);
									MessageHeader rh(mh);
									rh.source = mh.dest;
									rh.dest = mh.source;
									rh.start_time = microsecs();
									safeSend(*sock, response, strlen(response),rh);
									delete[] response;
								}
								else {
									/*
									char *response = new char[len+40];
									snprintf(response, len+40, "Unrecognised command: %s", buf);
									safeSend(*sock, response, strlen(response));
									 delete[] response;
									 */
								}
								delete[] buf;
							}
							delete command;
						}
						++i;

					}
					usleep(0);
					now = microsecs();
				}
				//NB_MSG << " @@@@@@@@@@@ " << count << " Processed\n";
			}
		}

		if (items[internals->SCHEDULER_ITEM].revents & ZMQ_POLLIN) {
#ifdef KEEPSTATS
			if (!scheduler_delay.running()) scheduler_delay.start();
#endif
			if (status == e_waiting && processing_state == eIdle) {
				size_t len = safeRecv(sched_sync, buf, 10, false, len, 0);
				if (len) {
					status = e_handling_sched;
#ifdef KEEPSTATS
					scheduler_delay.stop();
					avg_scheduler_time.start();
#endif
				}
				else {
					char buf[100];
					snprintf(buf, 100, "WARNING: scheduler sync returned zero length message");
					MessageLog::instance()->add(buf);
				}
			}
			else if (status == e_waiting_sched ) {
				size_t len = safeRecv(sched_sync, buf, 10, false, len, 0);
				if (len) {
					safeSend(sched_sync,"bye",3);
					status = e_waiting;
#ifdef KEEPSTATS
					avg_scheduler_time.update();
#endif
				}
				else {
					char buf[100];
					snprintf(buf, 100, "WARNING: scheduler sync returned zero length message");
					MessageLog::instance()->add(buf);
				}
			}
		}
		if (status == e_handling_sched) {
			size_t len = 0;
			safeSend(sched_sync,"continue",8);
			status = e_waiting_sched;
		}

		if (status == e_waiting && machines_have_work
			&& curr_t - last_checked_machines >= machine_check_delay)
		{

			if (processing_state == eIdle)
				processing_state = ePollingMachines;
			const int num_loops = 1;
			for (int i=0; i<num_loops; ++i) {
				if (processing_state == ePollingMachines)
				{
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
  							else iter++;
              }
							else iter++;
						}
					}

					if (!to_process.empty()) {
						DBG_SCHEDULER << "processing " << to_process.size() << " machines\n";
						MachineInstance::processAll(to_process, 150000, MachineInstance::NO_BUILTINS);
					}
					processing_state = eStableStates;
				}
				if (processing_state == eStableStates)
				{
					std::set<MachineInstance *> to_process;
					{	boost::recursive_mutex::scoped_lock lock(runnable_mutex);
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
							else iter++;
						}
					}

					if (!to_process.empty()) {
						DBG_SCHEDULER << "processing stable states\n";
						MachineInstance::checkStableStates(to_process, 150000);
					}
					if (i<num_loops-1)
						processing_state = ePollingMachines;
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
		// send a message to the ethercat thread requesting activation
		// or deactivation of the master
		if (status == e_waiting && !IOComponent::devices.empty()
				&& update_state == s_update_idle
				&& (machine.activationRequested() || machine.deactivationRequested())
			) {
			std::cout << "activation/deactivation requested\n";
			uint32_t size = 0;
			uint8_t stage = 1;
			while (true) {
				try {
					switch (stage) {
					case 1:
						{
							zmq::message_t iomsg(4);
							memcpy(iomsg.data(), (void*)&size, 4);
							ecat_out.send(iomsg, ZMQ_SNDMORE);
							++stage;
						}
					case 2:
						{
							uint8_t packet_type = 2;
							packet_type = machine.activationRequested() ? 3 : 4;
							zmq::message_t iomsg(1);
							memcpy(iomsg.data(), (void*)&packet_type, 1);
							ecat_out.send(iomsg);
							++stage;
						}
					}
					update_state = s_update_sent;
					break;
				}
				catch (const zmq::error_t &err) {
					if (zmq_errno() == EINTR) {
						std::cout << "interrupted when sending update (" << (unsigned int)stage << ")\n";
						continue;
					}
					else
						std::cerr << zmq_strerror(zmq_errno());
					assert(false);
				}
			}
		}
		else if (status == e_waiting && machine_is_ready && !IOComponent::devices.empty()
			&&
				(
				 IOComponent::updatesWaiting()
				 || IOComponent::getHardwareState() != IOComponent::s_operational
				)
		   ) {
#ifdef KEEPSTATS
			avg_update_time.start();
#endif
//			if (IOComponent::updatesWaiting() && IOComponent::updatesToSend())
//				std::cout << "ProcessingThread has new updates to send\n";

			if (update_state == s_update_idle) {
				IOUpdate *upd = 0;
				if (IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
					std::cout << "Sending defaults to EtherCAT\n";
					upd = IOComponent::getDefaults();
					assert(upd);
#if VERBOSE_DEBUG
					display(upd->data()); std::cout << ":"; display(upd->mask()); std::cout << "\n";
#endif
				}
				else
					upd = IOComponent::getUpdates();
				MEMCHECK();
				if (upd) {
					uint32_t size = upd->size();
					uint8_t stage = 1;
					while (true) {
						try {
							switch (stage) {
								case 1:
									{
										zmq::message_t iomsg(4);
										memcpy(iomsg.data(), (void*)&size, 4);
										ecat_out.send(iomsg, ZMQ_SNDMORE);
										++stage;
									}
								case 2:
									{
										uint8_t packet_type = 2;
										if (IOComponent::getHardwareState() != IOComponent::s_operational)
											packet_type = 1;
										zmq::message_t iomsg(1);
										memcpy(iomsg.data(), (void*)&packet_type, 1);
										ecat_out.send(iomsg, ZMQ_SNDMORE);
										++stage;
									}
								case 3:
									{
										zmq::message_t iomsg(size);
										memcpy(iomsg.data(), (void*)upd->data(), size);
										ecat_out.send(iomsg, ZMQ_SNDMORE);
//										std::cout << "sending to EtherCAT: "; display(upd->data()); std::cout << "\n";
										++stage;
									}
								case 4:
									{
										zmq::message_t iomsg(size);
										memcpy(iomsg.data(), (void*)upd->mask(), size);
//										std::cout << "using mask: "; display(upd->mask()); std::cout << "\n";
										ecat_out.send(iomsg);
										++stage;
									}
								default: ;
							}
							break;
						}
						catch (const zmq::error_t &err) {
							if (zmq_errno() == EINTR) {
								std::cout << "interrupted when sending update (" << (unsigned int)stage << ")\n";
								//usleep(50);
								continue;
							}
							else
								std::cerr << zmq_strerror(zmq_errno());
							assert(false);
						}
					}
//					std::cout << "update sent. Waiting for ack\n";
					MEMCHECK();
					delete upd;
					update_state = s_update_sent;
					IOComponent::updatesSent(true);
				}
				//else std::cout << "warning: getUpdate/getDefault returned null\n";
			}
			//else
			//	NB_MSG << "have updates but update state is not idle yet\n";
		}
		if (update_state == s_update_sent) {
			char buf[10];
			try {
				if (ecat_out.recv(buf, 10, ZMQ_DONTWAIT)) {
//					std::cout << "update acknowledged\n";
					update_state = s_update_idle;
					if (machine.activationRequested()) {
						if (strncmp(buf, "ok", 2) == 0)
							machine.requestActivation(false);
					}
					else if (machine.deactivationRequested()) {
						if (strncmp(buf, "ok", 2) == 0)
							machine.requestDeactivation(false);
					}
					else {
						if (IOComponent::getHardwareState() == IOComponent::s_hardware_init)
						{
							IOComponent::setHardwareState(IOComponent::s_operational);
							//activate_hardware();
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
		if (program_done) break;
	}
	//		std::cout << std::flush;
	//		model_mutex.lock();
	//		model_updated.notify_one();
	//		model_mutex.unlock();
	std::cout << "processing done\n";
}
