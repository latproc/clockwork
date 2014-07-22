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

#include "ecat_thread.h"

const char *EtherCATThread::ZMQ_Addr = "inproc://ecat_thread";

EtherCATThread::EtherCATThread() : status(e_collect), program_done(false), cycle_delay(1000) { }

void EtherCATThread::setCycleDelay(long new_val) { cycle_delay = new_val; }

bool EtherCATThread::waitForSync() {
	try {
		char buf[10];
		size_t len = sync_sock->recv(buf, 10);
		if (!len) return false; // interrupted
		return true;
	}
	catch (std::exception &ex) {
		return false;
	}
}

void EtherCATThread::operator()() {
	struct timeval then;
	gettimeofday(&then, 0);
	
	sync_sock = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REP);
	sync_sock->bind("inproc://ethercat_sync");
	
	while (!program_done && !waitForSync()) ;
	while (!program_done) {
		if (status == e_collect) {
			//if (!waitForSync()) continue;
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
			std::cout << delay << " " << then.tv_sec << "." << std::setw(6) << std::setfill('0') << then.tv_usec << "\n";
			//then = now;
	    	ECInterface::instance()->collectState();
			sync_sock->send("ok",2);
			status = e_update;
		}
		if (status == e_update) {
			if (!waitForSync()) continue;
			ECInterface::instance()->sendUpdates();
			//sync_sock->send("ok",2);
			status = e_collect;
		}
	}
}

