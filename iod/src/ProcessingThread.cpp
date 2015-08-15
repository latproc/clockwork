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

#if 0

#include "SimulatedRawInput.h"

typedef std::list<std::string> StringList;
std::map<std::string, StringList> wiring;


void checkInputs()
{
	std::map<std::string, StringList>::iterator iter = wiring.begin();
	while (iter != wiring.end())
	{
		const std::pair<std::string, StringList> &link = *iter++;
		Output *device = dynamic_cast<Output*>(IOComponent::lookup_device(link.first));
		if (device)
		{
			StringList::const_iterator linked_devices = link.second.begin();
			while (linked_devices != link.second.end())
			{
				Input *end_point = dynamic_cast<Input*>(IOComponent::lookup_device(*linked_devices++));
				SimulatedRawInput *in = 0;
				if (end_point)
					in  = dynamic_cast<SimulatedRawInput*>(end_point->access_raw_device());
				if (in)
				{
					if (device->isOn() && !end_point->isOn())
					{
						in->turnOn();
					}
					else if (device->isOff() && !end_point->isOff())
						in->turnOff();
				}
			}
		}
	}
}
#endif

	ProcessingThread::ProcessingThread(ControlSystemMachine &m, HardwareActivation &activator, IODCommandThread &cmd_interface)
: machine(m), sequence(0), cycle_delay(1000), status(e_waiting),
	activate_hardware(activator), command_interface(cmd_interface)
{
}

void ProcessingThread::stop()
{
	program_done = true;
	MessagingInterface::abort();
}


bool ProcessingThread::checkAndUpdateCycleDelay()
{
	const Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
	long delay = 100;
	if (cycle_delay_v && cycle_delay_v->iValue >= 100)
		delay = cycle_delay_v->iValue;
	if (delay != cycle_delay)
	{
		set_cycle_time(delay);
		//ECInterface::FREQUENCY = 1000000 / delay;
		//ECInterface::instance()->start();
		cycle_delay = delay;
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

#if 0
static void display(uint8_t *p) {
	int max = IOComponent::getMaxIOOffset();
	int min = IOComponent::getMinIOOffset();
	for (int i=min; i<=max; ++i) 
		std::cout << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i] << std::dec;
}
#endif

int ProcessingThread::pollZMQItems(int poll_wait, zmq::pollitem_t items[], 
		zmq::socket_t &ecat_sync, 
		zmq::socket_t &resource_mgr, 
		zmq::socket_t &dispatcher, 
		zmq::socket_t &scheduler, 
		zmq::socket_t &ecat_out)
{
	int res = 0;
	while (!program_done ) // && (status == e_waiting || status == e_waiting_cmd) )
	{
		try
		{
			long len = 0;
			char buf[10];
			res = zmq::poll(&items[0], 5, poll_wait);
			if (!res) return res;
			if (items[ECAT_ITEM].revents & ZMQ_POLLIN)
			{
				//DBG_MSG << "receiving data from EtherCAT\n";
				// the EtherCAT message carries a mask and data

				int64_t more;
				size_t more_size = sizeof (more);
				uint8_t stage = 1;
				//  Process all parts of the message
				while (true) {
					try {
						switch (stage) {
							case 1:
								{
									zmq::message_t message;
									// data length
									ecat_sync.recv(&message);
									size_t msglen = message.size();
									assert(msglen == sizeof(global_clock));
									memcpy(&global_clock, message.data(), msglen);
									++stage;
								}
							case 2: 
								{
									zmq::message_t message;
									// data length
									ecat_sync.recv(&message);
									size_t msglen = message.size();
									assert(msglen == sizeof(incoming_data_size));
									memcpy(&incoming_data_size, message.data(), msglen);
									len = incoming_data_size;
									if (len == 0) { stage = 4; break; }
									++stage;
								}
							case 3: 
								{
									// data
									ecat_sync.getsockopt( ZMQ_RCVMORE, &more, &more_size);
									assert(more);
									zmq::message_t message;
									ecat_sync.recv(&message);
									size_t msglen = message.size();
									assert(msglen == incoming_data_size);
									if (!incoming_process_data) incoming_process_data = new uint8_t[msglen];
									memcpy(incoming_process_data, message.data(), msglen);
									//std::cout << "got data: "; display(incoming_process_data); std::cout << "\n";
									++stage;
								}
							case 4: 
								{
									// mask
									zmq::message_t message;
									ecat_sync.getsockopt( ZMQ_RCVMORE, &more, &more_size);
									assert(more);
									ecat_sync.recv(&message);
									size_t msglen = message.size();
									assert(msglen == incoming_data_size);
									if (!incoming_process_mask) incoming_process_mask = new uint8_t[msglen];
									memcpy(incoming_process_mask, message.data(), msglen);
									//std::cout << "got mask: "; display(incoming_process_mask); std::cout << "\n";
									++stage;
								}
							default: ;
						}
						break;
					}
					catch(zmq::error_t err) {
						if (zmq_errno() == EINTR) {
							NB_MSG << "interrupted when sending update (" << (unsigned int)stage << ")\n";
							continue;
						}
					}
				}
				break;
			}
			break;
		}
		catch (std::exception ex)
		{
			if (errno == EINTR) continue; // TBD watch for infinite loop here
			const char *fnam = strrchr(__FILE__, '/');
			if (!fnam) fnam = __FILE__; else fnam++;
			NB_MSG << "Error " << zmq_strerror(errno)
				<< " in " << fnam << ":" << __LINE__ << "\n";
			break;
		}
	}
	return res;
}


const long *setupPropertyRef(const char *machine_name, const char *property_name) {
	MachineInstance *system = MachineInstance::find(machine_name);
	if (!system) return 0;
	const Value &prop(system->getValue(property_name));
	if (prop == SymbolTable::Null) {
		system->setValue(property_name, 0);
		const Value &prop(system->getValue(property_name));
		if (prop.kind != Value::t_integer) return 0;
		return &prop.iValue;
	}
	if (prop.kind != Value::t_integer) return 0;
	return &prop.iValue;
}

class AutoStatStorage {
public:
	// auto stats are linked to system properties
	AutoStatStorage(const char *time_name, const char *rate_name, unsigned int reset_every = 100)
	: start_time(0), last_update(0), total_polls(0), total_time(0),
		total_delays(0), rate_property(0), time_property(0),reset_point(reset_every)
	{
		if (time_name) time_property = setupPropertyRef("SYSTEM", time_name);
		if (rate_name) rate_property = setupPropertyRef("SYSTEM", rate_name);
	}
	void reset() { last_update = 0; total_polls = 0; total_time = 0; }
	void update(uint64_t now, uint64_t duration) {
		++total_polls;
		if (reset_point && total_polls>reset_point) {
			total_time = 0;
			total_polls = 1;
		}
		if (time_property) {
			total_time += duration;
			long *tp = (long*)time_property;
			*tp = total_time / total_polls;
		}
		if (rate_property) {
			uint64_t delay = now - last_update;
			total_delays += delay;
			long *rp = (long*)rate_property;
			*rp = total_delays / total_polls;
		}
	}
	void start() { start_time = nowMicrosecs(); }
	void update() {
		uint64_t now = nowMicrosecs();
		if (start_time) {update(now, now-start_time); }
		start_time = now;
	}
protected:
	uint64_t start_time;
	uint64_t last_update;
	uint32_t total_polls;
	uint64_t total_time;
	uint64_t total_delays;
	const long *rate_property;
	const long *time_property;
	unsigned int reset_point;
};

class AutoStat {
public:
	AutoStat(AutoStatStorage &storage) : storage_(storage) { start_time = nowMicrosecs(); }
	~AutoStat() {
		uint64_t now = nowMicrosecs();
		storage_.update(now, now-start_time);
	}

private:
	uint64_t start_time;
	AutoStatStorage &storage_;
};

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

#ifdef KEEPSTATS
	AutoStatStorage avg_poll_time("AVG_POLL_TIME", 0);
	AutoStatStorage avg_io_time("AVG_IO_TIME", 0);
	AutoStatStorage avg_iowork_time("AVG_IOWORK_TIME", 0);
	AutoStatStorage avg_plugin_time("AVG_PLUGIN_TIME", 0);
	AutoStatStorage avg_cmd_processing("AVG_PROCESSING_TIME", 0);
	AutoStatStorage avg_command_time("AVG_COMMAND_TIME", 0);
	AutoStatStorage avg_channel_time("AVG_CHANNEL_TIME", 0);
	AutoStatStorage avg_dispatch_time("AVG_DISPATCH_TIME", 0);
	AutoStatStorage avg_scheduler_time("AVG_SCHEDULER_TIME", 0);
	AutoStatStorage avg_clockwork_time("AVG_CLOCKWORK_TIME", 0);
	AutoStatStorage avg_update_time("AVG_UPDATE_TIME", 0);
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

	IOComponent::setHardwareState(IOComponent::s_hardware_init);

	safeSend(sched_sync,"go",2); // scheduled items
	usleep(10000);
	safeSend(dispatch_sync,"go",2); //  permit handling of events
	usleep(10000);
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

	MachineInstance *system = MachineInstance::find("SYSTEM");
	assert(system);

	enum { s_update_idle, s_update_sent } update_state = s_update_idle;
    
    if (IOComponent::devices.empty() ) {
        IOComponent::setHardwareState(IOComponent::s_operational);
        activate_hardware();
    }

	bool commands_started = false;

	enum { eIdle, eStableStates, ePollingMachines} processing_state = eIdle;
	std::set<IOComponent *>io_work_queue;
	while (!program_done)
	{
		unsigned int machine_check_delay = cycle_delay;
		machine.idle();
		if (machine.connected())
		{
			if (!machine_is_ready)
			{
				std::cout << "----------- Machine is Ready --------\n";
				machine_is_ready = true;
				BOOST_FOREACH(std::string &error, error_messages)
				{
					std::cerr << error << "\n";
					MessageLog::instance()->add(error.c_str());
				}
				safeSend(resource_mgr, "start", 5);
			}
		}
		else
			machine_is_ready = false;

#ifdef KEEPSTATS
		avg_poll_time.start();
#endif

		zmq::pollitem_t items[] =
		{
			{ ecat_sync, 0, ZMQ_POLLIN, 0 },
			{ resource_mgr, 0, ZMQ_POLLIN, 0 },
			{ dispatch_sync, 0, ZMQ_POLLIN, 0 },
			{ sched_sync, 0, ZMQ_POLLIN, 0 },
			{ ecat_out, 0, ZMQ_POLLIN, 0 }
		};
		char buf[100];
		int poll_wait = 2 * cycle_delay / 1000; // millisecs
		machine_check_delay = cycle_delay;
		if (poll_wait == 0) poll_wait = 1;
		uint64_t curr_t = 0;
		int systems_waiting = 0;
		while (!program_done)
		{
			curr_t = nowMicrosecs();
			//if (Watchdog::anyTriggered(curr_t))
			//	Watchdog::showTriggered(curr_t, true);
			systems_waiting = pollZMQItems(poll_wait, items, ecat_sync, resource_mgr, dispatch_sync, sched_sync, ecat_out);
			//DBG_MSG << "loop. status: " << status << " proc: " << processing_state
			//	<< " waiting: " << systems_waiting << "\n";
			if (systems_waiting > 0) break;
			if  (IOComponent::updatesWaiting() || !io_work_queue.empty()) break;
			if (curr_t - last_checked_machines > machine_check_delay && MachineInstance::workToDo()) break;
			if (!MachineInstance::pluginMachines().empty() && curr_t - last_checked_plugins >= 1000) break;
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
		DBG_MSG << "handling activity " << systems_waiting
			<< ( (items[ECAT_ITEM].revents & ZMQ_POLLIN) ? " ethercat" : "")
			<< ( (IOComponent::updatesWaiting()) ? " io components" : "")
			<< ( (!io_work_queue.empty()) ? " io work" : "")
			<< ( (curr_t - last_checked_machines > machine_check_delay) ? " machines" : "")
			<< ( (curr_t - last_checked_plugins >= 10000) ? " plugins" : "")
			<< "\n";
		if (IOComponent::updatesWaiting()) {
			extern std::set<IOComponent*> updatedComponentsOut;
			std::set<IOComponent*>::iterator iter = updatedComponentsOut.begin();
			while (iter != updatedComponentsOut.end()) std::cout << " " << (*iter++)->io_name;
			std::cout << " \n";
		}
#endif

		/* this loop prioritises ethercat processing but if a certain
			number of ethercat cycles have been processed with no 
			other activities being given time, we give other jobs
			some time anyway.
		*/
		if (items[ECAT_ITEM].revents & ZMQ_POLLIN)
		{
			static unsigned long total_mp_time = 0;
			static unsigned long mp_count = 0;
			uint8_t *mask_p = incoming_process_mask;
			int n = incoming_data_size;
			while (n && *mask_p == 0) { ++mask_p; --n; }
			if (n) { // io has indicated a change
				if (machine_is_ready)
				{
					//std::cout << "got EtherCAT data at byte " << (incoming_data_size-n) << "\n";
#ifdef KEEPSTATS
					AutoStat stats(avg_io_time);
#endif
					IOComponent::processAll( global_clock, incoming_data_size, incoming_process_mask, 
						incoming_process_data, io_work_queue);
				}
				else
					std::cout << "received EtherCAT data but machine is not ready\n";
			}
			safeSend(ecat_sync,"go",2);
		}
#if 0
		if (items[ECAT_ITEM].revents & ZMQ_POLLIN)
			std::cout << "ecat waiting\n";
#endif

		if (program_done) break;
		if  (machine_is_ready && processing_state != eStableStates &&  !io_work_queue.empty()) {
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
		if (processing_state == eIdle && !MachineInstance::pluginMachines().empty() && curr_t - last_checked_plugins >= 1000) {
#ifdef KEEPSTATS
			AutoStat stats(avg_plugin_time);
#endif
			MachineInstance::checkPluginStates();
		}

		if (status == e_waiting) {
			AutoStat stats(avg_channel_time);
			// poll channels
			Channel::handleChannels();
		}

		if (program_done) break;
		if (status == e_waiting)  {
			if (items[CMD_ITEM].revents & ZMQ_POLLIN) {
#ifdef KEEPSTATS
				AutoStat stats(avg_cmd_processing);
#endif
				//NB_MSG << "Processing: incoming data from client\n";
				size_t len = resource_mgr.recv(buf, 100, ZMQ_NOBLOCK);
				if (len) status = e_waiting_cmd;
			}
		}
		// process a pending command or two if there are any available
		if (status == e_waiting || status == e_waiting_cmd) {
#ifdef KEEPSTATS
			avg_command_time.start();
#endif
			IODCommand *command = command_interface.getCommand();
			// keep track of the time so we don't spend too much of it here
			uint64_t commands_start_time = nowMicrosecs();
			uint64_t now_t = commands_start_time;
			while (command && now_t-commands_start_time <100) {
				//NB_MSG << "Processing: received command: " << command->param(0) << "\n";
				(*command)();
				command_interface.putCompletedCommand(command);
				now_t = nowMicrosecs();
				if (now_t - commands_start_time < 100)
					command = command_interface.getCommand();
			}
			safeSend(resource_mgr, "ping", 4); // wakeup the client interface
#ifdef KEEPSTATS
			avg_command_time.update();
#endif
			status = e_waiting; //ange state back even though we haven't finished all the commands
		}
		if (program_done) break;

		if (status == e_waiting && items[DISPATCHER_ITEM].revents & ZMQ_POLLIN) {
			if (status == e_waiting) 
			{
				size_t len = dispatch_sync.recv(buf, 10, ZMQ_NOBLOCK);
				if (len) {
					status = e_handling_dispatch;
				}
			}
		}
		if (status == e_handling_dispatch) {
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
				safeRecv(dispatch_sync, buf, 10, true, len);
				safeSend(dispatch_sync,"bye",3);
				status = e_waiting;
			}
		}

		if (status == e_waiting && items[SCHEDULER_ITEM].revents & ZMQ_POLLIN) {
			if (processing_state != eIdle) {
				// cannot process scheduled events at present
				status = e_waiting;
			}
			else {
				if (status == e_waiting)
				{
					size_t len = safeRecv(sched_sync, buf, 10, false, len, 0);
					if (len) {
						status = e_handling_sched;
#ifdef KEEPSTATS
						avg_scheduler_time.start();
#endif
					}
				}
			}
		}
		if (status == e_handling_sched) {
			size_t len = 0;
			safeSend(sched_sync,"continue",8);
			status = e_waiting_sched;
		}
		if (status == e_waiting_sched) {
			size_t len = safeRecv(sched_sync, buf, 10, false, len, 0);
			if (len) {
				safeSend(sched_sync,"bye",3);
				status = e_waiting;
#ifdef KEEPSTATS
				avg_scheduler_time.update();
#endif
			}
		}

		if (status == e_waiting && curr_t - last_checked_machines >= machine_check_delay && machine_is_ready && MachineInstance::workToDo() )
		{

			if (processing_state == eIdle)
				processing_state = ePollingMachines;
			if (processing_state == ePollingMachines)
			{
#ifdef KEEPSTATS
				avg_clockwork_time.start();
#endif
				if (MachineInstance::processAll(150000, MachineInstance::NO_BUILTINS))
					processing_state = eStableStates;
			}
			if (processing_state == eStableStates)
			{
				if (MachineInstance::checkStableStates(150000)) {
					processing_state = eIdle;
					last_checked_machines = curr_t; // check complete
#ifdef KEEPSTATS
					avg_clockwork_time.update();
#endif
					;				}
			}
		}
		if (status == e_waiting && machine_is_ready && !IOComponent::devices.empty() &&
				(
				 IOComponent::updatesWaiting() 
				 || IOComponent::getHardwareState() != IOComponent::s_operational
				)
		   ) {
#ifdef KEEPSTATS
			avg_update_time.start();
#endif

			if (update_state == s_update_idle) {
				IOUpdate *upd = 0;
				if (IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
					//std::cout << "Sending defaults to EtherCAT\n";
					upd = IOComponent::getDefaults();
					assert(upd);
					//display(upd->data);
					//std::cout << "\n";
				}
				else
					upd = IOComponent::getUpdates();
				if (upd) {
					uint32_t size = upd->size;
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
										memcpy(iomsg.data(), (void*)upd->data, size);
										ecat_out.send(iomsg, ZMQ_SNDMORE);
										//std::cout << "sending to EtherCAT:\n";
										//display(upd->data);
										//std::cout << "\n";
										++stage;
									}
								case 4:
									{
										zmq::message_t iomsg(size);
										memcpy(iomsg.data(), (void*)upd->mask, size);
										ecat_out.send(iomsg);
										++stage;
									}
								default: ;
							}
							break;
						}
						catch (zmq::error_t err) {
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
					//std::cout << "update sent. Waiting for ack\n";
					delete upd;
					update_state = s_update_sent;
					IOComponent::updatesSent();
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
					//std::cout << "update acknowledged\n";
					update_state = s_update_idle;
					if (IOComponent::getHardwareState() == IOComponent::s_hardware_init)
					{
						IOComponent::setHardwareState(IOComponent::s_operational);
						activate_hardware();
					}
#ifdef KEEPSTATS
					avg_update_time.update();
#endif
				}
			}
			catch (zmq::error_t err) {
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

		if (program_done) break;
	}
	//		std::cout << std::flush;
	//		model_mutex.lock();
	//		model_updated.notify_one();
	//		model_mutex.unlock();
	std::cout << "processing done\n";
}
