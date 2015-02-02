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

#include <boost/foreach.hpp>

extern bool program_done;
extern bool machine_is_ready;
extern Statistics *statistics;

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

ProcessingThread::ProcessingThread(ControlSystemMachine &m, HardwareActivation &activator)
: machine(m), sequence(0), cycle_delay(1000), status(e_waiting),
	activate_hardware(activator)
{
}

void ProcessingThread::stop()
{
    program_done = true;
    MessagingInterface::abort();
}


bool ProcessingThread::checkAndUpdateCycleDelay()
{
    Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
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
#if 0
    while (len == 0)
    {
        try
        {
            len = resource_mgr.recv(buf, 10);
            resource_mgr.send("bye", 3);
            return;
        }
        catch (std::exception ex)
        {
            if (errno == EINTR) continue;
            std::cerr << "exception during wait for command processing " << zmq_strerror(errno) << "\n";
            return;
        }
    }
#endif
}

static uint8_t *incoming_process_data = 0;
static uint8_t *incoming_process_mask = 0;
static uint32_t incoming_data_size;

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
    while (!program_done && status == e_waiting)
    {
        try
        {
            long len = 0;
            char buf[10];
            res = zmq::poll(&items[0], 5, poll_wait);
            if (items[ECAT_ITEM].revents & ZMQ_POLLIN)
            {
				//std::cout << "receiving data from EtherCAT\n";
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
									assert(msglen == sizeof(incoming_data_size));
									memcpy(&incoming_data_size, message.data(), msglen);
									len = incoming_data_size;
									if (len == 0) { stage = 4; break; }
									++stage;
								}
								case 2: 
								{
									// data
									ecat_sync.getsockopt( ZMQ_RCVMORE, &more, &more_size);
									assert(more);
									zmq::message_t message;
									ecat_sync.recv(&message);
									size_t msglen = message.size();
									assert(msglen == incoming_data_size);
									if (incoming_process_data) delete incoming_process_data;
									incoming_process_data = new uint8_t[msglen];
									memcpy(incoming_process_data, message.data(), msglen);
									//std::cout << "got data: "; display(incoming_process_data); std::cout << "\n";
									++stage;
								}
								case 3: 
								{
									// mask
									zmq::message_t message;
									ecat_sync.getsockopt( ZMQ_RCVMORE, &more, &more_size);
									assert(more);
									ecat_sync.recv(&message);
									size_t msglen = message.size();
									assert(msglen == incoming_data_size);
									if (incoming_process_mask) delete incoming_process_mask;
									incoming_process_mask = new uint8_t[msglen];
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
							//std::cout << "interrupted when sending update (" << (unsigned int)stage << ")\n";
							usleep(50); 
							continue;
						}
					}
				}
				
                if (len) status = e_handling_ecat;
            }
            else if (items[CMD_ITEM].revents & ZMQ_POLLIN)
            {
                len = resource_mgr.recv(buf, 10, ZMQ_NOBLOCK);
                if (len) {
					status = e_handling_cmd;
				}
            }
            else if (items[DISPATCHER_ITEM].revents & ZMQ_POLLIN)
            {
                len = dispatcher.recv(buf, 10, ZMQ_NOBLOCK);
                if (len) {
					status = e_handling_dispatch;
				}
            }
            else if (items[SCHEDULER_ITEM].revents & ZMQ_POLLIN)
            {
                len = scheduler.recv(buf, 10, ZMQ_NOBLOCK);
                if (len) {
					status = e_handling_sched;
				}
            }
            break;
        }
        catch (std::exception ex)
        {
            if (errno == EINTR) continue; // TBD watch for infinite loop here
            std::cerr << "Error " << zmq_strerror(errno)
            << " in " << __FILE__ << ":" << __LINE__ << "\n";
            break;
        }
    }
    return res;
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
    
    zmq::socket_t dispatch_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    dispatch_sync.connect("inproc://dispatcher_sync");
    
    zmq::socket_t sched_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    sched_sync.connect("inproc://scheduler_sync");
    
    // used to permit command processing
    zmq::socket_t resource_mgr(*MessagingInterface::getContext(), ZMQ_REP);
    resource_mgr.bind("inproc://resource_mgr");
    
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


	enum { s_update_idle, s_update_sent } update_state = s_update_idle;

    while (!program_done)
    {
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
            }
        }
        else
            machine_is_ready = false;

        enum { eIdle, eStableStates, ePollingMachines} processingState = eIdle;
        struct timeval start_t, end_t;
        //boost::mutex::scoped_lock lock(thread_protection_mutex);
        gettimeofday(&start_t, 0);
        
        zmq::pollitem_t items[] =
        {
            { ecat_sync, 0, ZMQ_POLLIN, 0 },
            { resource_mgr, 0, ZMQ_POLLIN, 0 },
            { dispatch_sync, 0, ZMQ_POLLIN, 0 },
            { sched_sync, 0, ZMQ_POLLIN, 0 },
            { ecat_out, 0, ZMQ_POLLIN, 0 }
        };
        size_t len = 0;
        char buf[10];
		int poll_wait = 1000;
        while (!program_done && len == 0)
        {
			if (IOComponent::updatesWaiting()) poll_wait=10;
            if (pollZMQItems(poll_wait, items, ecat_sync, resource_mgr, dispatch_sync, 
					sched_sync, ecat_out)) break;
            if (MachineInstance::workToDo()) break;
        }
#if 0
       	if (items[ECAT_ITEM].revents & ZMQ_POLLIN)
       		std::cout << "ecat waiting\n";
       	if (items[CMD_ITEM].revents & ZMQ_POLLIN)
       		std::cout << "command waiting\n";
       	if (items[DISPATCHER_ITEM].revents & ZMQ_POLLIN)
       		std::cout << "dispatcher waiting\n";
#endif
        if (program_done) break;
        if (status == e_handling_cmd) {
			waitForCommandProcessing(resource_mgr);
		}
        if (program_done) break;
        
		if (status == e_handling_dispatch) {
			if (processingState != eIdle) {
				// cannot process dispatch events at present
				status = e_waiting;
                MachineInstance::forceIdleCheck();
			}
			else {
				safeSend(dispatch_sync,"continue",3);
				// wait for the dispatcher
				safeRecv(dispatch_sync, buf, 10, true, len);
				safeSend(dispatch_sync,"bye",3);
                MachineInstance::forceIdleCheck();
				status = e_waiting;
			}
            if (MachineInstance::workToDo()) {
                DBG_DISPATCHER << " work to do after dispatch\n";;
            }
            else { DBG_DISPATCHER << " no more work to do after dispatch\n"; }
		}
		else if (status == e_handling_sched) {
			if (processingState != eIdle) {
				// cannot process scheduled events at present
				status = e_waiting;
				std::cout << "can't handle scheduler\n";
			}
			else {
				safeSend(sched_sync,"continue",3);
				// wait for the dispatcher
				safeRecv(sched_sync, buf, 10, true, len);
				safeSend(sched_sync,"bye",3);
				//std::cout << "processing thread forcing a stable state check\n";
                //MachineInstance::forceStableStateCheck();
				status = e_waiting;
			}
		}
/*
        // poll channels
        zmq::pollitem_t *poll_items = 0;
        int active_channels = Channel::pollChannels(poll_items, 20, 0);
        if (active_channels) {
            Channel::handleChannels();
        }
*/		
		else if (items[ECAT_ITEM].revents & ZMQ_POLLIN)
        {
			static unsigned long total_mp_time = 0;
			static unsigned long mp_count = 0;
			//std::cout << "got EtherCAT data\n";
            if (machine_is_ready)
            {
                gettimeofday(&end_t, 0);
                IOComponent::processAll( incoming_data_size, incoming_process_mask, incoming_process_data);
                gettimeofday(&start_t, 0);
                delta = get_diff_in_microsecs(&start_t, &end_t);
				//MachineInstance *system = MachineInstance::find("SYSTEM");
				//assert(system);
				//total_mp_time += delta;
				//system->setValue("AVG_PROCESSING_TIME", total_mp_time*1000 / ++mp_count);
            }
			else
				std::cout << "received EtherCAT data but machine is not ready\n";
            //if (MachineInstance::workToDo()) {
            //    DBG_SCHEDULER << " work to do. scheduling another check\n";
			//std::cout << "EtherCAT data update complete\n";
			safeSend(ecat_sync,"go",2);
            //}
            //else { DBG_SCHEDULER << " no more work to do\n"; }
        }
        if (machine_is_ready && MachineInstance::workToDo() )
        {
            if (processingState == eIdle)
                processingState = ePollingMachines;
            if (processingState == ePollingMachines)
            {
                if (MachineInstance::processAll(150000, MachineInstance::NO_BUILTINS))
                    processingState = eStableStates;
            }
            if (processingState == eStableStates)
            {
                if (MachineInstance::checkStableStates(150000))
                    processingState = eIdle;
            }
		}
		if (machine_is_ready && 
				(
					IOComponent::updatesWaiting() 
					|| IOComponent::getHardwareState() == IOComponent::s_hardware_init
				)
			) {
			if (update_state == s_update_idle) {
				IOUpdate *upd = 0;
				if (IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
					//std::cout << "Sending defaults to EtherCAT\n";
					upd = IOComponent::getDefaults();
					//display(upd->data);
					//std::cout << "\n";
					activate_hardware();
					IOComponent::setHardwareState(IOComponent::s_operational);
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
										if (IOComponent::getHardwareState() == IOComponent::s_hardware_init)
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
								usleep(50); 
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
			}
			//else
			//	NB_MSG << "have updates but update state is not idle yet\n";
        }
		if (update_state == s_update_sent) {
			char buf[10];
			size_t len;
			try {
			//safeRecv(ecat_out, buf, 10, true, len);
				if (ecat_out.recv(buf, 10, ZMQ_DONTWAIT)) {
					//std::cout << "update acknowledged\n";
					update_state = s_update_idle;
				}
			}
			catch (zmq::error_t err) {
				assert(zmq_errno() == EINTR);
			}
		}
        
        // periodically check to see if the cycle time has been changed
        // more work is needed here since the signaller needs to be told about this
		gettimeofday(&end_t, 0);
        uint64_t now_usecs = end_t.tv_sec *1000000 + end_t.tv_usec;
        if (now_usecs - last_checked_cycle_time > 100000L) {
            last_checked_cycle_time = end_t.tv_sec *1000000 + end_t.tv_usec;
            checkAndUpdateCycleDelay();
        }

        status = e_waiting;
        if (program_done) break;
    }
    //		std::cout << std::flush;
    //		model_mutex.lock();
    //		model_updated.notify_one();
    //		model_mutex.unlock();
	std::cout << "processing done\n";
}
