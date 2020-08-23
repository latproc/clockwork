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
#include "clockwork.h"
#include "options.h"
#include "Statistic.h"
#include "ecat_thread.h"
#include "DebugExtra.h"
#include "MachineInstance.h"
#include "IOComponent.h"
//#include "SetStateAction.h"

#define USE_RTC 1

#ifdef USE_RTC
#include <sys/ioctl.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <errno.h>

#endif

#define VERBOSE_DEBUG 0

extern bool machine_is_ready;
const char *EtherCATThread::ZMQ_Addr = "inproc://ecat_thread";
static bool machine_was_ready = false;
uint64_t next_ecat_receive = 0;

EtherCATThread::EtherCATThread() : status(e_collect), program_done(false), cycle_delay(1000), keep_alive(4000),last_ping(0) { 
}

void EtherCATThread::setCycleDelay(long new_val) { cycle_delay = new_val; }

bool EtherCATThread::waitForSync(zmq::socket_t &sync_sock) {
	char buf[10];
	size_t response_len;
	return safeRecv(sync_sock, buf, 10, true, response_len, 0);
}

#ifndef USE_RTC
//#define USE_SIGNALLER 1
#endif

static bool recv(zmq::socket_t &sock, zmq::message_t &msg) {
	bool received = false;
	try { 
		received = sock.recv(&msg, ZMQ_DONTWAIT);
	}
	catch (zmq::error_t &err) {
		if (zmq_errno() == EINTR) { 
			std::cout << "ecat_thread interrupted in recv\n";
			return false;
		}
		assert(false);
	}
	return received;
}

bool EtherCATThread::checkAndUpdateCycleDelay()
{
	if (cycle_delay != get_cycle_time())
	{
		cycle_delay = get_cycle_time();
		std::cout << "setting cycle time to " << cycle_delay << "\n";
		ECInterface::FREQUENCY = 1000000 / cycle_delay;
		return true;
	}
	return false;
}

#if VERBOSE_DEBUG
static void display(uint8_t *p, size_t len) {
	for (size_t i=0; i<len; ++i) 
		std::cout << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i] << std::dec;
}
#endif

enum DriverState { s_driver_init, s_driver_operational };
DriverState driver_state = s_driver_init;
// data from clockwork should be one of these two types.
// process data will only be used if default data has been sent
const int DEFAULT_DATA = 1;
const int PROCESS_DATA = 2;
const int ACTIVATE_REQUEST = 3;
const int DEACTIVATE_REQUEST = 4;

size_t default_data_size = 0;
uint8_t *default_data = 0;
uint8_t *default_mask = 0;
uint8_t *last_data = 0;
uint8_t *dbg_mask = 0;
uint8_t *cmp_data = 0;

void setDefaultData(size_t len, uint8_t *data, uint8_t *mask) {
	if (default_data) delete[] default_data;
	if (default_mask) delete[] default_mask;
	default_data_size = len;
	if (!default_data) default_data = new uint8_t[len];
	memcpy(default_data, data, len);
	if (!default_mask) default_mask = new uint8_t[len];
	memcpy(default_mask, data, len);

#if VERBOSE_DEBUG
	std::cout << "default data: "; display(default_data, len); std::cout << "\n";
	std::cout << "default mask: "; display(default_mask, len); std::cout << "\n";
#endif
} 

#ifdef USE_SIGNALLER
void sync(zmq::socket_t &clock_sync) {
	waitForSync(clock_sync);
}
#else
#ifdef USE_RTC
void sync(int rtc) {
	static uint64_t last_read_sync_check = 0;
	{
		unsigned long val = 0; 
		unsigned long period = 1000000 / ECInterface::FREQUENCY;
		uint64_t t = 0;
		while (1) {
			int nread = read(rtc, &val, sizeof(val));
			if (nread<0) { perror("rtc-read"); exit(1); }
			t = microsecs();
			if (last_read_sync_check) {
				if ( (t - last_read_sync_check) * 2 < period) continue;
			}
			if (t - last_read_sync_check < 100) 
				std::cout << t - last_read_sync_check << "\n";
			last_read_sync_check = t;
			break;
		}
	}

	// every 1000 times through here check and resync with the system cycle time
	static int count = 0;
	if (++count >= 1000) {
		count = 0;
		unsigned long freq = ECInterface::FREQUENCY;
		unsigned long rtc_val;
		int rc = ioctl(rtc, RTC_IRQP_READ, &rtc_val);
		if (rc == -1) { 
			if (errno == EBADF) {
				rtc = open("/dev/rtc", 0);
				if (rtc == -1) { perror("open rtc"); exit(1); }
			}
			perror("read rtc"); exit(1); 
		}
		else 
			freq = rtc_val;
		if ( freq != ECInterface::FREQUENCY ) {
			std::cout << "-------------------- adjusting frequency: " 
				<< freq << "->" << ECInterface::FREQUENCY << "\n";
			unsigned long saved_freq = freq;
			freq = ECInterface::FREQUENCY;
			rc = ioctl(rtc, RTC_IRQP_SET, freq);
			if (rc == -1) { 
				perror("set rtc freq"); 
				freq = saved_freq; // will retry
			}
			else {
				rc = ioctl(rtc, RTC_IRQP_READ, &rtc_val);
				if (rc != -1) std::cout << "frequency is now " << rtc_val << "\n";
			}
		}
	}
}
#else
void sync() {
	uint64_t now = nowMicrosecs();
	int64_t delta = now - then;
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
	}
	gettimeofday(&then,0);
}
#endif
#endif

int EtherCATThread::sendMultiPart( zmq::socket_t *sync_sock, uint64_t global_clock ) {
	// sending a four-part message, each stage may be interrupted
	// so we use a try-catch around the whole process 
	ECInterface::instance()->setMinIOIndex(IOComponent::getMinIOOffset());
	ECInterface::instance()->setMaxIOIndex(IOComponent::getMaxIOOffset());
	uint32_t size = ECInterface::instance()->getProcessDataSize();


	uint8_t stage = 1;
#if VERBOSE_DEBUG
	bool found_change = false;
#endif
	while(true) {
		try {
			switch(stage) {
			case 1:
			{
#if VERBOSE_DEBUG
//DBG_MSG << " send stage: " << (int)stage << " " << sizeof(global_clock) << "\n";
//{uint8_t *chk = new uint8_t[1]; memset(chk, 0, 1); delete[] chk; }
#endif
				zmq::message_t iomsg(sizeof(global_clock));
				memcpy(iomsg.data(), (void*)&global_clock, sizeof(global_clock) ); 
				sync_sock->send(iomsg, ZMQ_SNDMORE);
				++stage;
			}
			case 2:
			{
#if VERBOSE_DEBUG
//DBG_MSG << " send stage: " << (int)stage << " " << "4\n";
//{uint8_t *chk = new uint8_t[1]; memset(chk, 0, 1); delete[] chk; }
#endif
				zmq::message_t iomsg(4);
				memcpy(iomsg.data(), (void*)&size, 4); 
				sync_sock->send(iomsg, ZMQ_SNDMORE);
				++stage;
			}
			case 3:
			{
#if VERBOSE_DEBUG
//DBG_MSG << " send stage: " << (int)stage << " " << size << "\n";
//{uint8_t *chk = new uint8_t[1]; memset(chk, 0, 1); delete[] chk; }
#endif
				uint8_t *upd_data = ECInterface::instance()->getUpdateData();
#if VERBOSE_DEBUG
				if (driver_state == s_driver_init) {
					std::cout << "ecat_thread sending :";
					display(upd_data, size);
					std::cout << ":\n";
				}
#endif
				zmq::message_t iomsg(size);
				memcpy(iomsg.data(), (void*)upd_data, size); 
				sync_sock->send(iomsg, ZMQ_SNDMORE);
				++stage;
#if VERBOSE_DEBUG
				if (size && last_data ==0) { 
					last_data = new uint8_t[size];
					memset(last_data, 0, size);
					dbg_mask = new uint8_t[size];
					memset(dbg_mask, 0xff, size);
					cmp_data = new uint8_t[size];
					memset(cmp_data, 0, size);
				}
				if (size) {
					uint8_t *p = upd_data, *q = cmp_data, *msk = dbg_mask;
					for (size_t ii=0; ii<size; ++ii) *q++ = *p++ & *msk++;
					if (memcmp( cmp_data, last_data, size) != 0) {
						found_change = true;
						std::cout << "ec "; display(dbg_mask, size); std::cout << "\n";
						std::cout << "ec>"; display(upd_data, size); std::cout << "\n";
					}
					else found_change = false;
					memcpy(last_data, cmp_data, size);
				}
#endif
			}
			case 4:
			{
#if VERBOSE_DEBUG
//DBG_MSG << " send stage: " << (int)stage << " " << size <<"\n";
#endif
				zmq::message_t iomsg(size);
				uint8_t *mask = ECInterface::instance()->getUpdateMask(); 
				memcpy(iomsg.data(), (void*)mask,size); 
				sync_sock->send(iomsg);
#if VERBOSE_DEBUG
				if (found_change) {
					std::cout << "ec&"; display(mask, size); std::cout << "\n";
				}
//{uint8_t *chk = new uint8_t[1]; memset(chk, 0, 1); delete[] chk; }
#endif
				++stage;
				break;
			}
			default: ;
			} // end of switch
			break;
		}
		catch (zmq::error_t &err) {
			if (zmq_errno() == EINTR) { 
				DBG_ETHERCAT << "interrupted when sending update (" << stage << ")\n";
				usleep(50); 
				continue; 
			}
			else {
				std::cerr << "EtherCAT exception " << err.what() 
				<< " error " << zmq_strerror(errno) << "sending EtherCAT data to clockwork\n";
			}
		}
		break;
	}
	return stage;
}

uint64_t updateClock(uint64_t global_clock) {
#ifdef USE_DC
	// distributed clocks. TBD
	static uint64_t last_ref_time =  ECInterface::instance()->getReferenceTime();
	uint32_t ref_time =  ECInterface::instance()->getReferenceTime();
	if (ref_time) {
		uint32_t last_ref32 = last_ref_time % 0x100000000;
		int64_t delta_ref = 0;
		if (last_ref32 > ref_time) { // rollover
			//std::cerr << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n";
			delta_ref = 0x100000000 + (uint64_t)ref_time;
			//std::cerr << std::hex << std::setw(8) << ref_time << std::dec << "\n";
			//std::cerr << std::hex << std::setw(16) << delta_ref << std::dec << "\n";
			//std::cerr << std::hex << std::setw(16) << last_ref32 << std::dec << "\n";
			delta_ref -= last_ref32;
			//std::cerr << std::hex << std::setw(16) << delta_ref << std::dec << "\n";
		}
		else
			delta_ref = ref_time - last_ref_time;
		//int64_t err = delta_ref - period;
		//if ( fabs(err) > (float)period/10)
		std::cerr << "ref: " << ref_time << " cycle: " << delta_ref << " error: " << err << "\n";
		last_ref_time += delta_ref;
		global_clock += delta_ref;
	}
		else
			global_clock += period;
#else
		//global_clock += period;
		global_clock = microsecs();
#endif
	return global_clock;
}

bool EtherCATThread::getEtherCatResponse( zmq::socket_t *sync_sock, uint64_t global_clock,
			Statistic *keep_alive_stat  ) {
	try {
		char buf[10];
		int len = 0;
		if ( (len = sync_sock->recv(buf, 10, ZMQ_DONTWAIT)) > 0 ) {
			if (keep_alive) {
				uint64_t this_ping_time = nowMicrosecs();
				if (last_ping && keep_alive_stat)
					keep_alive_stat->add(keep_alive - (this_ping_time - last_ping));
				last_ping = this_ping_time;
			}
			DBG_ETHERCAT << "ecat_thread in state e_update got response from clockwork, len = " << len << "\n";
			return true;
		}
		return false;
	}
	catch (zmq::error_t &ex) {
		if (zmq_errno() != EINTR) {
			DBG_ETHERCAT << "EtherCAT error " << zmq_strerror(errno) << "checking for update from clockwork\n";
		}
		else {
			DBG_ETHERCAT << "EtherCAT exception " << ex.what() 
				<< " error " << zmq_strerror(errno) << "checking for update from clockwork\n";
		}
		return false;
	}
	return true;
}
bool EtherCATThread::getClockworkMessage(zmq::socket_t &out_sock, bool ec_ok) {
	zmq::message_t output_update;
	uint8_t packet_type = 0;
	uint32_t len = 0;
	{
	zmq::message_t iomsg;
	bool received = recv(out_sock, iomsg);
	if (!received) return false;
	DBG_ETHERCAT << "received output from clockwork; size: " << iomsg.size() << "\n";
	assert(iomsg.size() == sizeof(len));
	memcpy(&len, iomsg.data(), sizeof(len));
	DBG_ETHERCAT << "expecting incoming message len " << len << "\n";
	}
	int64_t more = 0;
	size_t more_size = sizeof(more);
	out_sock.getsockopt(ZMQ_RCVMORE, &more, &more_size);
	assert(more);

	uint8_t *cw_data = 0;
	uint8_t *cw_mask = 0;
	// Packet Type
	{
		zmq::message_t iomsg;
		bool received = recv(out_sock, iomsg);
		if (!received) return false;

		DBG_ETHERCAT << "reading packet type " << sizeof(packet_type) << " byte(s)\n";
		assert(iomsg.size() == sizeof(packet_type));
		memcpy(&packet_type, iomsg.data(), sizeof(packet_type));
		if (driver_state == s_driver_init) {
			if (packet_type == DEFAULT_DATA) {
				DBG_MSG << "received initial values from clockwork; size: " 
					<< len << " packet: " << (int)packet_type << "\n";
			}
			else {
				DBG_MSG << "received process values from clockwork; size: " 
						<< len << " packet: " << (int)packet_type << "\n";
			}
		}
//		else {
//			DBG_ETHERCAT << "WARNING: read packet type but driver is not in state init\n";
//		}
		if (packet_type == DEFAULT_DATA || packet_type == PROCESS_DATA) {
			int64_t more = 0;
			size_t more_size = sizeof(more);
			out_sock.getsockopt(ZMQ_RCVMORE, &more, &more_size);
			assert(more);
		}
	}
	if (packet_type == DEFAULT_DATA || packet_type == PROCESS_DATA) {
		{
			zmq::message_t iomsg;
			recv(out_sock, iomsg);
			cw_data = new uint8_t[iomsg.size()];
			memcpy(cw_data, iomsg.data(), iomsg.size());
			assert(iomsg.size() == len);

			if (packet_type == DEFAULT_DATA) {
				//TBD. this hack removes a dependency inside ECInterface but more work is needed
				// to cleanup this initialisation
				ECInterface::instance()->setAppProcessMask(IOComponent::getProcessMask(), iomsg.size());
			}
		}
#if VERBOSE_DEBUG
		if (!default_data) {
			assert(packet_type == DEFAULT_DATA);
		  	std::cout << "received default data from driver\n";
				display(cw_data, len);
				std::cout << "\n";
		}
  else if (packet_type == PROCESS_DATA) {
        std::cout << "p:";
				display(cw_data, len);
        std::cout << "\n";
  }
#endif

		more = 0;
		more_size = sizeof(more);
		out_sock.getsockopt(ZMQ_RCVMORE, &more, &more_size);
		assert(more);
		{
			zmq::message_t iomsg;
			recv(out_sock, iomsg);
			size_t msglen = iomsg.size();
			assert(msglen == len);
			//assert((long)msglen != -1);
			if (msglen > 0) {
				cw_mask = new uint8_t[msglen];
				memcpy(cw_mask, iomsg.data(), iomsg.size());
			}
		}
	}
	if (packet_type == ACTIVATE_REQUEST) {
		std::cout << "---------------- Activate\n";
		if (ECInterface::instance()->activate())
			safeSend(out_sock,"ok", 2);
		else
			safeSend(out_sock,"nack", 2);
	}
	else if (packet_type == DEACTIVATE_REQUEST) {
		std::cout << "---------------- Deactivate\n";
		IOComponent::reset();
		assert(ECInterface::instance()->deactivate());
		IOComponent::setHardwareState(IOComponent::s_hardware_preinit);
		safeSend(out_sock,"ok", 2);
	}
	else if (packet_type == DEFAULT_DATA) {
		setDefaultData(len, cw_data, cw_mask);
		delete[] cw_mask;
		delete[] cw_data;
		safeSend(out_sock,"ok", 2);
	}
	else if (packet_type == PROCESS_DATA) {
		if ( driver_state == s_driver_init) {
			if (!default_data) 
				std::cerr << "WARNING: Getting process data from driver with no default set\n";
			else {
				driver_state = s_driver_operational;

			}
		}
#if VERBOSE_DEBUG
		else {
			std::cout << "!"; display(cw_mask, len); std::cout << "\n";
			std::cout << "<"; display(cw_data, len); std::cout << "\n";
		}
#endif
		if (ec_ok && default_data) { // only update the domain if default data has been setup
			ECInterface::instance()->updateDomain(len, cw_data, cw_mask);
#if VERBOSE_DEBUG
      // check if our mask is not the same as the data that cw has
      // 
      if (last_data) {
        bool done_header = false;
				uint8_t *upd_data = ECInterface::instance()->getUpdateData();

        for (size_t i = 0; i<len; ++i) {
          if (cw_data[i] != upd_data[i]) {
            if (!done_header) { done_header = true; std::cout << "process data diffs at bytes: "; }
            std::cout << i << " ";
          }
        } 
        if (done_header) std::cout << "\n";
      }
#endif
    }
		else if (!default_data)
			std::cout << "ecat_thread: no default data set yet, cannot update domain\n";
		delete[] cw_mask;
		delete[] cw_data;
		safeSend(out_sock,"ok", 2);
	}
	return true;
}

void EtherCATThread::operator()() {
    pthread_setname_np(pthread_self(), "iod ethercat");
	Statistic *keep_alive_stat = new Statistic("keep alive margin");
	Statistic::add(keep_alive_stat);

#ifdef USE_RTC
	rtc = open("/dev/rtc", 0);
	if (rtc == -1) { perror("open rtc"); exit(1); }
	unsigned long freq = ECInterface::FREQUENCY;

	int rc = ioctl(rtc, RTC_IRQP_SET, freq);
	if (rc == -1) { perror("set rtc freq"); exit(1); }

	rc = ioctl(rtc, RTC_IRQP_READ, &freq);
	if (rc == -1) { perror("ioctl"); exit(1); }
	std::cout << "Real time clock: freq set to : " << freq << "\n";

	rc = ioctl(rtc, RTC_PIE_ON, 0);
	if (rc == -1) { perror("enable rtc pie"); exit(1); }
#endif
	uint64_t then = nowMicrosecs();
	ECInterface::instance()->setReferenceTime(then % 0x100000000);
	
	sync_sock = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REP);
	sync_sock->bind("inproc://ethercat_sync");

	// when clockwork has output to send to EtherCAT it sends it here
	zmq::socket_t out_sock(*MessagingInterface::getContext(), ZMQ_REP);
	out_sock.bind("inproc://ethercat_output");

#ifdef USE_SIGNALLER
	zmq::socket_t clock_sync(*MessagingInterface::getContext(), ZMQ_SUB);
	clock_sync.connect("tcp://localhost:10241");
	int res = zmq_setsockopt (clock_sync, ZMQ_SUBSCRIBE, "", 0);
    assert (res == 0);
#endif
            
	uint64_t global_clock = 0;
	bool first_run = true;
	// assume all is ok with EtherCAT. If modules go offline this flips and we stop polling
	static bool ec_ok = true; 
	while (!program_done && !waitForSync(*sync_sock)) usleep(100);
	while (!program_done) {
#ifdef USE_RTC
		sync(rtc);	
#else
#ifdef USE_SIGNALLER
		sync(clock_sync);	
#else
		sync();
#endif
#endif
		uint64_t now = nowMicrosecs();
		ECInterface::instance()->setReferenceTime(now % 0x100000000);
		uint64_t period = 1000000/freq;
		int num_updates = 0;

		// TBD the following line was missing, preventing the ec_ok logic from working
		if (machine_is_ready && !machine_was_ready) machine_was_ready = true;

		if (machine_was_ready) {
			if (ec_ok != machine_is_ready) {
				std::cout << "ethercat thread is now " << machine_is_ready << "\n";
				ec_ok = machine_is_ready;
				if (last_data) { delete last_data; last_data = 0; }
				if (dbg_mask) { delete dbg_mask; dbg_mask = 0; }
				if (cmp_data) { delete cmp_data; cmp_data = 0; }
				if (!machine_is_ready) {
					driver_state = s_driver_init;
				}
			}
		}

		// keep polling ethercat if we are not online but do not
		// exchange clockwork machine state
		next_ecat_receive = microsecs() + period / 2;
		ECInterface::instance()->receiveState();

		if (machine_is_ready && ECInterface::instance()->getProcessMask()) {
			global_clock = updateClock(global_clock);
			if (status == e_collect) {
				num_updates = ECInterface::instance()->collectState();
			}

			// send all process domain data once the domain is operational
			// check keep-alives on the clockwork communications channel
			//   four periods: 4 * period / 1000 = period/250
			bool need_ping = keep_alive>0 && (last_ping + keep_alive - period < now) ? true : false;


			// if we have collected data from EtherCAT, send it to clockwork
			if ( status == e_collect
					&& (first_run || num_updates || need_ping)) {
				if (driver_state == s_driver_operational) first_run = false;
				need_ping = false;
				int stage = sendMultiPart(sync_sock, global_clock);
#if VERBOSE_DEBUG
				std::cout << "send done\n";
#endif
				assert(stage == 5);
				status = e_update; // time to send process data to EtherCAT
			}
			if (status == e_update && getEtherCatResponse(sync_sock, global_clock, keep_alive_stat)) 
				status = e_collect;
		}	
//		else 
//			std::cout << "machine is not ready; no state collected\n";
	
		// check for communication from clockwork
		if( getClockworkMessage(out_sock, ec_ok) 
				&& microsecs() < next_ecat_receive - 300) {
//			std::cout << "ecat thread got clockwork message. next ecat: " << next_ecat_receive << "\n";
			usleep(20);
		}
		usleep(20);

		ECInterface::instance()->sendUpdates();
		checkAndUpdateCycleDelay();
	}
	keep_alive_stat->report(std::cout);
}

