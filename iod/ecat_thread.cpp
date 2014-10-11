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


void EtherCATThread::operator()() {
    pthread_setname_np(pthread_self(), "iod ethercat");

#ifdef USE_RTC
	rtc = open("/dev/rtc", 0);
	if (rtc == -1) { perror("open rtc"); exit(1); }
	unsigned long period = 600;

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

	zmq::socket_t clock_sync(*MessagingInterface::getContext(), ZMQ_SUB);
	clock_sync.connect("tcp://localhost:10241");
	int res = zmq_setsockopt (clock_sync, ZMQ_SUBSCRIBE, "", 0);
    assert (res == 0);
            
	
	while (!program_done && !waitForSync(*sync_sock)) ;
	while (!program_done) {
		if (status == e_collect) {
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
		perror("read rtc"); exit(1); }
		if (rc == 0) std::cout << "zero bytes read from rtc\n";
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
	    int n = ECInterface::instance()->collectState();
			if (n || IOComponent::hasUpdates()) {
				sync_sock->send("ok",2);
				status = e_update;
			}
			else // no clockwork changes, just poll the io
				ECInterface::instance()->sendUpdates();
		}
		if (status == e_update) {
			if (!waitForSync(*sync_sock)) continue;
			ECInterface::instance()->sendUpdates();
			status = e_collect;
		}
	}
}

