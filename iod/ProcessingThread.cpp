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

ProcessingThread::ProcessingThread(ControlSystemMachine &m)
: machine(m), sequence(0), cycle_delay(1000), status(e_waiting)
{
}

void ProcessingThread::stop()
{
    program_done = true;
    MessagingInterface::abort();
}


void ProcessingThread::checkAndUpdateCycleDelay()
{
    Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
    long delay = 100;
    if (cycle_delay_v && cycle_delay_v->iValue >= 100)
        delay = cycle_delay_v->iValue;
    if (delay != cycle_delay)
    {
        ECInterface::FREQUENCY = 1000000 / delay;
        //ECInterface::instance()->start();
        cycle_delay = delay;
    }
}

void ProcessingThread::waitForCommandProcessing(zmq::socket_t &resource_mgr)
{
    // handshake to give the command handler access to shared resources for a while
    // if it has requested it.
    // first stage is to give access second stage is to assert we are taking access back
    
    resource_mgr.send("go", 2);
	status = e_waiting;
    char buf[10];
    size_t len = 0;
	safeRecv(resource_mgr, buf, 10, true, len);
	resource_mgr.send("bye", 3);
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


int ProcessingThread::pollZMQItems(zmq::pollitem_t items[], zmq::socket_t &ecat_sync, zmq::socket_t &resource_mgr, zmq::socket_t &dispatcher, zmq::socket_t &scheduler)
{
	int res = 0;
    while (!program_done && status == e_waiting)
    {
        try
        {
            long len = 0;
            char buf[10];
            res = zmq::poll(&items[0], 4, 100);
            if (items[ECAT_ITEM].revents & ZMQ_POLLIN)
            {
                len = ecat_sync.recv(buf, 10, ZMQ_NOBLOCK);
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
    
#if 0
	boost::thread::attributes attrs;
	// set portable attributes
	// ...
	//attr.set_stack_size(4096*10);
#if defined(BOOST_THREAD_PLATFORM_WIN32)
    // ... window version
#elif defined(BOOST_THREAD_PLATFORM_PTHREAD)
    // ... pthread version
    //pthread_attr_setschedpolicy(attr.get_native_handle(), SCHED_RR);
    pthread_setname_np(attrs.get_native_handler(), "iod processing");
#else
#error "Boost threads unavailable on this platform"
#endif
#endif

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

    sched_sync.send("go",2); // scheduled items
    usleep(10000);
    dispatch_sync.send("go",2); //  permit handling of events
    usleep(10000);
    ecat_sync.send("go",2); // collect state
    usleep(10000);
	
    checkAndUpdateCycleDelay();

    uint64_t last_checked_cycle_time = 0;

#ifdef USE_EXPERIMENTAL_IDLE_LOOP

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
            { sched_sync, 0, ZMQ_POLLIN, 0 }
        };
        size_t len = 0;
        char buf[10];
        while (!program_done && len == 0)
        {
            if (pollZMQItems(items, ecat_sync, resource_mgr, dispatch_sync, sched_sync)) break;
            if (MachineInstance::workToDo()) break;
        }
        //		if (items[ECAT_ITEM].revents & ZMQ_POLLIN)
        //			std::cout << "ecat waiting\n";
        //		if (items[CMD_ITEM].revents & ZMQ_POLLIN)
        //			std::cout << "command waiting\n";
        //		if (items[DISPATCHER_ITEM].revents & ZMQ_POLLIN)
        //			std::cout << "dispatcher waiting\n";
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
				dispatch_sync.send("continue",3);
				// wait for the dispatcher
				safeRecv(dispatch_sync, buf, 10, true, len);
				dispatch_sync.send("bye",3);
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
			}
			else {
				sched_sync.send("continue",3);
				// wait for the dispatcher
				safeRecv(sched_sync, buf, 10, true, len);
				sched_sync.send("bye",3);
                MachineInstance::forceStableStateCheck();
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
            
            if (machine_is_ready)
            {
                
                delta = get_diff_in_microsecs(&start_t, &end_t);
                cycle_delay_stat->add(delta);
                IOComponent::processAll();
                gettimeofday(&end_t, 0);
                
                delta = get_diff_in_microsecs(&end_t, &start_t);
                statistics->io_scan_time.add(delta);
                delta2 = delta;
                gettimeofday(&end_t, 0);
                delta = get_diff_in_microsecs(&end_t, &start_t);
                statistics->points_processing.add(delta - delta2);
                delta2 = delta;
                
                /*
                long remain = cycle_delay / 2;
                if (processingState == eIdle)
                    processingState = ePollingMachines;
                if (processingState == ePollingMachines)
                {
                    if (MachineInstance::processAll(50000, MachineInstance::NO_BUILTINS))
                        processingState = eIdle;
                    gettimeofday(&end_t, 0);
                    delta = get_diff_in_microsecs(&end_t, &start_t);
                    remain -= delta;
                    statistics->machine_processing.add(delta - delta2);
                    delta2 = delta;
                }
                if (processingState == eIdle) processingState = eStableStates;
                
                if (processingState == eStableStates)
                {
                    if (MachineInstance::checkStableStates(50000))
                        processingState = eIdle;
                    gettimeofday(&end_t, 0);
                    delta = get_diff_in_microsecs(&end_t, &start_t);
                    statistics->auto_states.add(delta - delta2);
                    delta2 = delta;
                }
                */
            }
            if (MachineInstance::workToDo()) {
                DBG_SCHEDULER << " work to do. scheduling another check\n";
                ecat_sync.send("go",2);
            }
            else { DBG_SCHEDULER << " no more work to do\n"; }
        }
        if (machine_is_ready && MachineInstance::workToDo() )
        {
            if (processingState == eIdle)
                processingState = ePollingMachines;
            if (processingState == ePollingMachines)
            {
                if (MachineInstance::processAll(50000, MachineInstance::NO_BUILTINS))
                    processingState = eStableStates;
            }
            if (processingState == eStableStates)
            {
                if (MachineInstance::checkStableStates(50000))
                    processingState = eIdle;
            }
            
        }
        
        
        // periodically check to see if the cycle time has been changed
        // more work is needed here since the signaller needs to be told about this
        uint64_t now_usecs = end_t.tv_sec *1000000 + end_t.tv_usec;
        if (now_usecs - last_checked_cycle_time > 100000) {
            last_checked_cycle_time = end_t.tv_sec *1000000 + end_t.tv_usec;
            checkAndUpdateCycleDelay();
        }

        status = e_waiting;
        if (program_done) break;
    }
#else
    while (!program_done)
    {
        enum { eIdle, eStableStates, ePollingMachines} processingState = eIdle;
        struct timeval start_t, end_t;
        //boost::mutex::scoped_lock lock(thread_protection_mutex);
        gettimeofday(&start_t, 0);
        
        zmq::pollitem_t items[] =
        {
            { ecat_sync, 0, ZMQ_POLLIN, 0 },
            { resource_mgr, 0, ZMQ_POLLIN, 0 },
            { dispatch_sync, 0, ZMQ_POLLIN, 0 },
            { sched_sync, 0, ZMQ_POLLIN, 0 }
        };
        size_t len = 0;
        char buf[10];
        while (!program_done && len == 0)
        {
            if (pollZMQItems(items, ecat_sync, resource_mgr, dispatch_sync, sched_sync)) break;
        }
        //		if (items[ECAT_ITEM].revents & ZMQ_POLLIN)
        //			std::cout << "ecat waiting\n";
        //		if (items[CMD_ITEM].revents & ZMQ_POLLIN)
        //			std::cout << "command waiting\n";
        //		if (items[DISPATCHER_ITEM].revents & ZMQ_POLLIN)
        //			std::cout << "dispatcher waiting\n";
        if (program_done) break;
        if (status == e_handling_cmd) {
            waitForCommandProcessing(resource_mgr);
        }
        if (program_done) break;
        
        if (status == e_handling_dispatch) {
            if (processingState != eIdle) {
                // cannot process dispatch events at present
                status = e_waiting;
            }
            else {
                dispatch_sync.send("continue",3);
                // wait for the dispatcher
                safeRecv(dispatch_sync, buf, 10, true, len);
                //				buf[len] = 0; std::cout << "dispatcher thread said: " << buf << "\n";
                dispatch_sync.send("bye",3);
                status = e_waiting;
            }
        }
        if (status == e_handling_sched) {
            if (processingState != eIdle) {
                // cannot process scheduled events at present
                status = e_waiting;
            }
            else {
                sched_sync.send("continue",3);
                // wait for the dispatcher
                safeRecv(sched_sync, buf, 10, true, len);
                //				buf[len] = 0; std::cout << "dispatcher thread said: " << buf << "\n";
                sched_sync.send("bye",3);
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
        if (items[ECAT_ITEM].revents & ZMQ_POLLIN)
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
                
                if (machine_is_ready)
                {
                    
                    delta = get_diff_in_microsecs(&start_t, &end_t);
                    cycle_delay_stat->add(delta);
                    IOComponent::processAll();
                    gettimeofday(&end_t, 0);
                    
                    delta = get_diff_in_microsecs(&end_t, &start_t);
                    statistics->io_scan_time.add(delta);
                    delta2 = delta;
                    
#if 0
                    checkInputs(); // simulated wiring between inputs and outputs
#endif
                    gettimeofday(&end_t, 0);
                    delta = get_diff_in_microsecs(&end_t, &start_t);
                    statistics->points_processing.add(delta - delta2);
                    delta2 = delta;
                    
                    long remain = cycle_delay / 2;
                    if (processingState == eIdle)
                        processingState = ePollingMachines;
                    if (processingState == ePollingMachines)
                    {
                        if (MachineInstance::processAll(50000, MachineInstance::NO_BUILTINS))
                            processingState = eIdle;
                        gettimeofday(&end_t, 0);
                        delta = get_diff_in_microsecs(&end_t, &start_t);
                        remain -= delta;
                        statistics->machine_processing.add(delta - delta2);
                        delta2 = delta;
                    }
                    if (processingState == eIdle)
                    {
                        
#if 0
                        if (items[DISPATCHER_ITEM].revents & ZMQ_POLLIN) {
                            dispatch_sync.send("continue",3);
                            // wait for the dispatcher
                            safeRecv(dispatch_sync, buf, 10, true, len);
                            dispatch_sync.send("bye",3);
                            status = e_waiting;
                        }
#endif
#if 0
                        // wait for message sending to complete
                        zmq::pollitem_t items[] = { { dispatch_sync, 0, ZMQ_POLLIN, 0 } };
                        size_t len = 0;
                        while (len == 0)
                        {
                            zmq::poll(&items[0], 1, 100);
                            if (items[0].revents & ZMQ_POLLIN)
                            {
                                char buf[10];
                                len = dispatch_sync.recv(buf, 10, ZMQ_NOBLOCK);
                            }
                        }
                        
                        Scheduler::instance()->idle();
                        gettimeofday(&end_t, 0);
                        delta = get_diff_in_microsecs(&end_t, &start_t);
                        statistics->dispatch_processing.add(delta - delta2);
                        delta2 = delta;
#endif
                        processingState = eStableStates;
                    }
                    if (processingState == eStableStates)
                    {
                        if (MachineInstance::checkStableStates(50000))
                            processingState = eIdle;
                        gettimeofday(&end_t, 0);
                        delta = get_diff_in_microsecs(&end_t, &start_t);
                        statistics->auto_states.add(delta - delta2);
                        delta2 = delta;
                    }
                    
                    // periodically check to see if the cycle time has been changed
                    // more work is needed here since the signaller needs to be told about this
                    uint64_t now_usecs = end_t.tv_sec *1000000 + end_t.tv_usec;
                    if (now_usecs - last_checked_cycle_time > 100000) {
                        last_checked_cycle_time = end_t.tv_sec *1000000 + end_t.tv_usec;
                        checkAndUpdateCycleDelay();
                    }
                    
                }
            }
            ecat_sync.send("go",2); // update io
            status = e_waiting;
        }
        if (program_done) break;
    }
#endif
    //		std::cout << std::flush;
    //		model_mutex.lock();
    //		model_updated.notify_one();
    //		model_mutex.unlock();
	std::cout << "processing done\n";
}
