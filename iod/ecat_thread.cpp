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
#include "ECInterface.h"
#include <sstream>
#include <stdio.h>
#include <zmq.hpp>
#include <sys/stat.h>

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <list>
#include <map>
#include <utility>
#include <fstream>
#ifndef EC_SIMULATOR
#include <tool/MasterDevice.h>
#endif

#define __MAIN__
#include <stdio.h>
#include "Logger.h"
#include "MessagingInterface.h"
#include "IOComponent.h"
#include "clockwork.h"
#include "options.h"
#include "ecat_thread.h"

#define USE_RTC 1

#ifdef USE_RTC
#include <sys/ioctl.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <errno.h>

#endif

extern bool machine_is_ready;
const char *EtherCATThread::ZMQ_Addr = "inproc://ecat_thread";

EtherCATThread::EtherCATThread() : status(e_collect), program_done(false), cycle_delay(1000) { 
}

void EtherCATThread::setCycleDelay(long new_val) { cycle_delay = new_val; }

bool EtherCATThread::waitForSync(zmq::socket_t &sync_sock) {
	char buf[10];
	size_t response_len;
	return safeRecv(sync_sock, buf, 10, true, response_len);
}

#ifndef USE_RTC
//#define USE_SIGNALLER 1
#endif

static bool recv(zmq::socket_t &sock, zmq::message_t &msg) {
	bool received = false;
	while (true) { 
		try { 
			received = sock.recv(&msg, ZMQ_DONTWAIT);
			break;
		}
		catch (zmq::error_t err) {
			if (zmq_errno() == EINTR) { 
				//std::cout << "ecat_thread interrupted in recv\n";
				//usleep(50000); 
				return false;
				break; 
			}
			assert(false);
		}
	}
	return received;
}

bool EtherCATThread::checkAndUpdateCycleDelay()
{
#if 0
    Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
    long delay = 100;
    if (cycle_delay_v && cycle_delay_v->iValue >= 100)
        delay = cycle_delay_v->iValue;
#endif
    if (cycle_delay != get_cycle_time())
    {
        cycle_delay = get_cycle_time();
        ECInterface::FREQUENCY = 1000000 / cycle_delay;
		return true;
    }
	return false;
}

static void display(uint8_t *p) {
	int max = IOComponent::getMaxIOOffset();
	int min = IOComponent::getMinIOOffset();
    for (int i=min; i<=max; ++i) 
		std::cout << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i];
}

void EtherCATThread::operator()() {
    pthread_setname_np(pthread_self(), "iod ethercat");

#ifdef USE_RTC
	rtc = open("/dev/rtc", 0);
	if (rtc == -1) { perror("open rtc"); exit(1); }
	unsigned long period = ECInterface::FREQUENCY;

	int rc = ioctl(rtc, RTC_IRQP_SET, period);
	if (rc == -1) { perror("set rtc period"); exit(1); }

	rc = ioctl(rtc, RTC_IRQP_READ, &period);
	if (rc == -1) { perror("ioctl"); exit(1); }
	std::cout << "Real time clock: period set to : " << period << "\n";

	rc = ioctl(rtc, RTC_PIE_ON, 0);
	if (rc == -1) { perror("enable rtc pie"); exit(1); }
#endif
	struct timeval then;
	gettimeofday(&then, 0);
	
	sync_sock = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REP);
	sync_sock->bind("inproc://ethercat_sync");

	// when clockwork has output to send to EtherCAT it sends it here
	zmq::socket_t out_sock(*MessagingInterface::getContext(), ZMQ_REP);
	out_sock.bind("inproc://ethercat_output");

	zmq::socket_t clock_sync(*MessagingInterface::getContext(), ZMQ_SUB);
	clock_sync.connect("tcp://localhost:10241");
	int res = zmq_setsockopt (clock_sync, ZMQ_SUBSCRIBE, "", 0);
    assert (res == 0);
            
	bool first_run = true;
	while (!program_done && !waitForSync(*sync_sock)) ;
	while (!program_done) {
		//if (status == e_collect) {
#ifdef USE_SIGNALLER
			waitForSync(clock_sync);
#else
#ifdef USE_RTC
			long rtc_val;
			int rc = read(rtc, &rtc_val, sizeof(rtc_val));
			if (rc == -1) { 
				if (errno == EBADF) {
					rtc = open("/dev/rtc", 0);
					if (rtc == -1) { perror("open rtc"); exit(1); }
				}
				perror("read rtc"); exit(1); 
			}
			if (rc == 0) std::cout << "zero bytes read from rtc\n";
			if (period != ECInterface::FREQUENCY) {
				period = ECInterface::FREQUENCY;
				rc = ioctl(rtc, RTC_IRQP_SET, period);
				if (rc == -1) { perror("set rtc period"); exit(1); }
			}
#else
			struct timeval now;
			gettimeofday(&now,0);
			int64_t delta = (uint64_t)(now.tv_sec - then.tv_sec) * 1000000 
						+ ( (uint64_t)now.tv_usec - (uint64_t)then.tv_usec);
			int64_t delay = cycle_delay-delta-100;
			if (delay>0) {
				struct timespec sleep_time;
				sleep_time.tv_sec = delay / 1000000;
				sleep_time.tv_nsec = (delay * 1000) % 1000000000L;
				int rc;
				struct timespec remaining;
				while ( (rc = nanosleep(&sleep_time, &remaining) == -1) ) {
					sleep_time = remaining;
				}
	   		//gettimeofday(&now,0);
	   		//delta = (uint64_t)(now.tv_sec - then.tv_sec) * 1000000 
				//			+ ( (uint64_t)now.tv_usec - (uint64_t)then.tv_usec);
	       	//delay = cycle_delay-delta-20;
			}

			gettimeofday(&then,0);
			//then.tv_usec += cycle_delay;
			//while (then.tv_usec > 1000000) { then.tv_usec-=1000000; then.tv_sec++; }
			//			std::cout << delay << " " << then.tv_sec << "." << std::setw(6) << std::setfill('0') << then.tv_usec << "\n";
			//then = now;
#endif
#endif
			int n = 0;
			if (!machine_is_ready) {
				std::cout << "machine is not ready..\n";
				ECInterface::instance()->receiveState();
				ECInterface::instance()->sendUpdates();
				continue;
			}
			else {
				ECInterface::instance()->receiveState();
				if (status == e_collect)
					n = ECInterface::instance()->collectState();
			}
			//if (n) 
				//std::cout << n << " changes when collecting state\n";
			// send all process domain data once the domain is operational

			if ( status == e_collect && (first_run || n) && machine_is_ready) {
				first_run = false;
				std::cout << "io changed, forwarding to clockwork\n";
				uint32_t size = ECInterface::instance()->getProcessDataSize();

				uint8_t stage = 1;
				while(true) {
					try {
						switch(stage) {
							case 1:
							{
								zmq::message_t iomsg(4);
								memcpy(iomsg.data(), (void*)&size, 4); 
								sync_sock->send(iomsg, ZMQ_SNDMORE);
								++stage;
							}
							case 2:
							{
								zmq::message_t iomsg(size);
								std::cout << "sending ";
								display(ECInterface::instance()->getUpdateData());
								std::cout << "\n";
								memcpy(iomsg.data(), (void*)ECInterface::instance()->getUpdateData(),size); 
								sync_sock->send(iomsg, ZMQ_SNDMORE);
								++stage;
							}
							case 3:
							{
								zmq::message_t iomsg(size);
								uint8_t *mask = ECInterface::instance()->getUpdateMask(); 
								memcpy(iomsg.data(), (void*)mask,size); 
								sync_sock->send(iomsg);
								++stage;
							}
							default: ;
						}
						break;
					}
					catch (zmq::error_t err) {
						if (zmq_errno() == EINTR) { 
							std::cout << "interrupted when sending update (" << stage << ")\n";
							usleep(50000); 
							continue; 
						}
					}
				}
				std::cout << "update sent to clockwork\n";
				status = e_update;
			}
			if (status == e_update) {

				try {
					char buf[10];
					if (sync_sock->recv(buf, 10, ZMQ_DONTWAIT)) {
						status = e_collect;
						std::cout << "got response from clockwork\n";
					}
				}
				catch (zmq::error_t) {
					if (zmq_errno() != EINTR) {
						std::cout << "EtherCAT error " << zmq_strerror(errno) << "checking for update from clockwork\n";
					}
				}
				//std::cout << "sent EtherCAT update, waiting for response\n";
				//char buf[10];
				//size_t len;
				//safeRecv(*sync_sock, buf, 10, true, len);
				//std::cout << "EtherCAT update done\n";
			}

	
			// check for communication from clockwork
			zmq::message_t output_update;
			
			bool received = true;
			while ( received) {
				uint32_t len = 0;
				{
				zmq::message_t iomsg;
				received = recv(out_sock, iomsg);
				if (!received) break;
				//std::cout << "received output from clockwork "
				//	<< "size: " << iomsg.size() << "\n";
				assert(iomsg.size() == sizeof(len));
				memcpy(&len, iomsg.data(), iomsg.size());
				}
				int64_t more = 0;
				size_t more_size = sizeof(more);
				out_sock.getsockopt(ZMQ_RCVMORE, &more, &more_size);
				assert(more);

				uint8_t *cw_data;
				{
				zmq::message_t iomsg;
				recv(out_sock, iomsg);
				cw_data = new uint8_t[iomsg.size()];
				memcpy(cw_data, iomsg.data(), iomsg.size());
				}
	
				more = 0;
				more_size = sizeof(more);
				out_sock.getsockopt(ZMQ_RCVMORE, &more, &more_size);
				assert(more);
				uint8_t *cw_mask;
				{
				zmq::message_t iomsg;
				recv(out_sock, iomsg);
				cw_mask = new uint8_t[iomsg.size()];
				memcpy(cw_mask, iomsg.data(), iomsg.size());
				}
	
				//std::cout << "acknowledging receipt of clockwork output\n";
				safeSend(out_sock,"ok", 2);
				ECInterface::instance()->updateDomain(len, cw_data, cw_mask);
			}
			ECInterface::instance()->sendUpdates();
			checkAndUpdateCycleDelay();
		}	
	//}
}

