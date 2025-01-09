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

#ifdef __linux__
#define USE_CHRONO 1
#endif
#include "clock_sync.h"

#include "ECInterface.h"
#include "IOInterface.h"
#include "MessageLog.h"
#include "value.h"
#include "clock.h"

#include <stdint.h>
#include <ostream>
#include <sstream>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zmq.hpp>

#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <fstream>
#include <list>
#include <map>
#include <utility>
#ifndef EC_SIMULATOR
#include <iostream>
#endif

#define __MAIN__
#include "DebugExtra.h"
#include "IOComponent.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessagingInterface.h"
#include "Statistic.h"
#include "clockwork.h"
#include "ecat_thread.h"
#include "options.h"
#include <stdio.h>
//#include "SetStateAction.h"

//#define USE_RTC 1

extern bool machine_is_ready; // unsafe?
const char *EtherCATThread::ZMQ_Addr = "inproc://ecat_thread";
static bool machine_was_ready = false;
uint64_t next_ecat_receive = 0;

EtherCATThread::EtherCATThread()
    : status(e_collect), program_done(false), cycle_delay(1000), keep_alive(4000), last_ping(0) {}

void EtherCATThread::setCycleDelay(long new_val) {
    cycle_delay = new_val;
}

bool EtherCATThread::waitForSync(zmq::socket_t &sync_sock) {
    char buf[10];
    size_t response_len;
    return safeRecv(sync_sock, buf, 10, true, response_len, 0);
}

static bool recv(zmq::socket_t &sock, zmq::message_t &msg) {
    bool received = false;
    try {
        received = sock.recv(&msg, ZMQ_DONTWAIT);
    }
    catch (const zmq::error_t &err) {
        if (zmq_errno() == EINTR) {
            auto buf = "ecat_thread interrupted in recv";
            MessageLog::instance()->add(buf);
            std::cerr << buf << "\n" << std::flush;
            return false;
        }
        else {
            std::stringstream ss;
            ss << "ZMQ exception " << __FILE__ << ":" << __LINE__ << " "
               << err.what() << " error " << zmq_strerror(errno) << "\n";
            std::cerr << ss.str() << "\n";
        }
    }
    return received;
}

bool EtherCATThread::checkAndUpdateCycleDelay() {
    // FIXME: potentially not thread safe
    auto new_cycle_time = get_cycle_time();
    if (cycle_delay != new_cycle_time) {
        std::cerr << "setting cycle time " << cycle_delay << " -> " << new_cycle_time << "\n";
        cycle_delay = new_cycle_time;
        ECInterface::FREQUENCY = 1000000 / cycle_delay;
        return true;
    }
    return false;
}


enum DriverState { s_driver_init, s_driver_operational };
DriverState driver_state = s_driver_init;

size_t default_data_size = 0;
uint8_t *default_data = 0;
uint8_t *default_mask = 0;
uint8_t *last_data = 0;
uint8_t *dbg_mask = 0;
uint8_t *cmp_data = 0;

void setDefaultData(size_t len, uint8_t *data, uint8_t *mask) {
    if (default_data) {
        delete[] default_data;
    }
    if (default_mask) {
        delete[] default_mask;
    }
    default_data_size = len;
    if (!default_data) {
        default_data = new uint8_t[len];
    }
    memcpy(default_data, data, len);
    if (!default_mask) {
        default_mask = new uint8_t[len];
    }
    memcpy(default_mask, data, len);

#if VERBOSE_DEBUG
    std::cout << "default data: ";
    display(default_data, len);
    std::cout << "\n";
    std::cout << "default mask: ";
    display(default_mask, len);
    std::cout << "\n";
#endif
}


int EtherCATThread::sendMultiPart(zmq::socket_t *sync_sock, uint64_t global_clock) {
    // sending a four-part message, each stage may be interrupted
    // so we use a try-catch around the whole process
    ECInterface::instance()->data.setMinIOIndex(IOComponent::getMinIOOffset());
    ECInterface::instance()->data.setMaxIOIndex(IOComponent::getMaxIOOffset());
    uint32_t size = ECInterface::instance()->data.getProcessDataSize();
    DBG_ETHERCAT_PACKETS << "ecat_thread sending multipart message, size: " << size << "\n";

    uint8_t stage = 1;
#if VERBOSE_DEBUG
    bool found_change = false;
#endif
    uint8_t *upd_data = ECInterface::instance()->data.getUpdateData();
    if (upd_data == nullptr) {
        return 0;
    }
    while (true) {
        try {
            switch (stage) {
            case 1: {
#if VERBOSE_DEBUG
                DBG_MSG << " send stage: " << (int)stage << " " << sizeof(global_clock) << "\n";
#endif
                zmq::message_t iomsg(sizeof(global_clock));
                memcpy(iomsg.data(), (void *)&global_clock, sizeof(global_clock));
                sync_sock->send(iomsg, ZMQ_SNDMORE);
                ++stage;
            }
            case 2: {
#if VERBOSE_DEBUG
                DBG_MSG << " send stage: " << (int)stage << " " << "4\n";
#endif
                zmq::message_t iomsg(4);
                memcpy(iomsg.data(), (void *)&size, 4);
                sync_sock->send(iomsg, ZMQ_SNDMORE);
                ++stage;
            }
            case 3: {
#if VERBOSE_DEBUG
                DBG_MSG << " send stage: " << (int)stage << " " << size << "\n";
#endif
#if VERBOSE_DEBUG
                if (driver_state == s_driver_init) {
                    std::cout << "ecat_thread sending :";
                    display(upd_data, size);
                    std::cout << ":\n";
                }
#endif
                zmq::message_t iomsg(size);
                memcpy(iomsg.data(), (void *)upd_data, size);
                sync_sock->send(iomsg, ZMQ_SNDMORE);
                ++stage;
#if VERBOSE_DEBUG
                if (size && last_data == 0) {
                    last_data = new uint8_t[size];
                    memset(last_data, 0, size);
                    dbg_mask = new uint8_t[size];
                    memset(dbg_mask, 0xff, size);
                    cmp_data = new uint8_t[size];
                    memset(cmp_data, 0, size);
                }
                if (size) {
                    uint8_t *p = upd_data, *q = cmp_data, *msk = dbg_mask;
                    for (size_t ii = 0; ii < size; ++ii) {
                        *q++ = *p++ & *msk++;
                    }
                    if (memcmp(cmp_data, last_data, size) != 0) {
                        found_change = true;
                        std::cout << "ec ";
                        display(dbg_mask, size);
                        std::cout << "\n";
                        std::cout << "ec>";
                        display(upd_data, size);
                        std::cout << "\n";
                    }
                    else {
                        found_change = false;
                    }
                    memcpy(last_data, cmp_data, size);
                }
#endif
            }
            case 4: {
#if VERBOSE_DEBUG
                //DBG_MSG << " send stage: " << (int)stage << " " << size <<"\n";
#endif
                zmq::message_t iomsg(size);
                uint8_t *mask = ECInterface::instance()->data.getUpdateMask();
                memcpy(iomsg.data(), (void *)mask, size);
                sync_sock->send(iomsg);
#if VERBOSE_DEBUG
                if (found_change) {
                    std::cout << "ec&";
                    display(mask, size);
                    std::cout << "\n";
                }
#endif
                ++stage;
                break;
            }
            default:;
            } // end of switch
            break;
        }
        catch (const zmq::error_t &err) {
            if (zmq_errno() == EINTR) {
                DBG_ETHERCAT_PACKETS << "interrupted when sending update (" << stage << ")\n";
                usleep(50);
                continue;
            }
            else {
                std::cerr << "EtherCAT exception " << err.what() << " error " << zmq_strerror(errno)
                          << "sending EtherCAT data to clockwork\n";
            }
        }
        break;
    }
    return stage;
}


bool EtherCATThread::getEtherCatResponse(zmq::socket_t *sync_sock, uint64_t global_clock,
                                         Statistic *keep_alive_stat) {
    try {
        char buf[10];
        int len = 0;
        if ((len = sync_sock->recv(buf, 10, ZMQ_DONTWAIT)) > 0) {
            if (keep_alive) {
                uint64_t this_ping_time = nowMicrosecs();
                if (last_ping && keep_alive_stat) {
                    keep_alive_stat->add(keep_alive - (this_ping_time - last_ping));
                }
                last_ping = this_ping_time;
            }
            DBG_ETHERCAT_PACKETS
                << "ecat_thread in state e_update got response from clockwork, len = " << len
                << "\n";
            return true;
        }
        return false;
    }
    catch (const zmq::error_t &ex) {
        if (zmq_errno() != EINTR) {
            DBG_ETHERCAT << "EtherCAT error " << zmq_strerror(errno)
                         << "checking for update from clockwork\n";
        }
        else {
            DBG_ETHERCAT << "EtherCAT exception " << ex.what() << " error " << zmq_strerror(errno)
                         << "checking for update from clockwork\n";
        }
        return false;
    }
}
bool EtherCATThread::getClockworkMessage(zmq::socket_t &out_sock, bool ec_ok) {
    zmq::message_t output_update;
    IOInterface::MessageType packet_type;
    uint32_t len = 0;
    {
        zmq::message_t iomsg;
        bool received = recv(out_sock, iomsg);
        if (!received) {
            return false;
        }
        DBG_ETHERCAT_PACKETS << "received output from clockwork; size: " << iomsg.size() << "\n";
        assert(iomsg.size() == sizeof(len));
        memcpy(&len, iomsg.data(), sizeof(len));
        DBG_ETHERCAT_PACKETS << "expecting incoming message len " << len << "\n";
    }
    int64_t more = 0;
    size_t more_size = sizeof(more);
    out_sock.getsockopt(ZMQ_RCVMORE, &more, &more_size);
    assert(more);

    uint8_t *cw_data = nullptr;
    uint8_t *cw_mask = nullptr;
    // Packet Type
    {
        zmq::message_t iomsg;
        bool received = recv(out_sock, iomsg);
        if (!received) {
            return false;
        }

        DBG_ETHERCAT_PACKETS << "reading packet type " << sizeof(packet_type) << " byte(s)\n";
        assert(iomsg.size() == sizeof(packet_type));
        memcpy(&packet_type, iomsg.data(), sizeof(packet_type));
        if (driver_state == s_driver_init) {
            if (packet_type == IOInterface::MessageType::DEFAULT_DATA) {
                DBG_ETHERCAT_PACKETS << "received initial values from clockwork; size: " << len
                                     << " packet: " << (int)packet_type << "\n";
            }
            else {
                DBG_ETHERCAT_PACKETS << "received process values from clockwork; size: " << len
                                     << " packet: " << (int)packet_type << "\n";
            }
        }
        //      else {
        //          DBG_ETHERCAT << "WARNING: read packet type but driver is not in state init\n";
        //      }
        if (packet_type == IOInterface::MessageType::DEFAULT_DATA ||
            packet_type == IOInterface::MessageType::PROCESS_DATA) {
            int64_t more = 0;
            size_t more_size = sizeof(more);
            out_sock.getsockopt(ZMQ_RCVMORE, &more, &more_size);
            assert(more);
        }
    }
    if (packet_type == IOInterface::MessageType::DEFAULT_DATA ||
        packet_type == IOInterface::MessageType::PROCESS_DATA) {
        {
            zmq::message_t iomsg;
            recv(out_sock, iomsg);
            if (cw_data) {
                delete[] cw_data;
            }
            cw_data = new uint8_t[iomsg.size()];
            memcpy(cw_data, iomsg.data(), iomsg.size());
            assert(iomsg.size() == len);

            if (packet_type == IOInterface::MessageType::DEFAULT_DATA) {
                //TBD. this hack removes a dependency inside ECInterface but more work is needed
                // to cleanup this initialisation
                ECInterface::instance()->data.setDataSize(iomsg.size());
                ECInterface::instance()->data.setAppProcessMask(IOComponent::getProcessMask(),
                                                           iomsg.size());
            }
        }
#if VERBOSE_DEBUG
        if (!default_data) {
            assert(packet_type == IOInterface::MessageType::DEFAULT_DATA);
            DBG_ETHERCAT_PACKETS << "received default data from driver\n";
            display(cw_data, len);
            DBG_ETHERCAT_PACKETS << "\n";
        }
        else if (packet_type == IOInterface::MessageType::PROCESS_DATA) {
            std::cout << "p:";
            display(cw_data, len);
            DBG_ETHERCAT_PACKETS << "\n";
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
            if (msglen > 0) {
                assert(!cw_mask);
                cw_mask = new uint8_t[msglen];
                memcpy(cw_mask, iomsg.data(), msglen);
            }
        }
    }
    if (packet_type == IOInterface::MessageType::ACTIVATE_REQUEST) {
        std::cout << "---------------- Activate\n";
        if (ECInterface::instance()->activate()) {
            safeSend(out_sock, "ok", 2);
        }
        else {
            safeSend(out_sock, "nack", 2);
        }
    }
    else if (packet_type == IOInterface::MessageType::DEACTIVATE_REQUEST) {
        std::cout << "---------------- Deactivate\n";
        IOComponent::reset();
        assert(ECInterface::instance()->deactivate());
        IOComponent::setHardwareState(IOComponent::s_hardware_preinit);
        safeSend(out_sock, "ok", 2);
    }
    else if (packet_type == IOInterface::MessageType::DEFAULT_DATA) {
        setDefaultData(len, cw_data, cw_mask);
        delete[] cw_mask;
        delete[] cw_data;
        cw_mask = cw_data = nullptr;
        safeSend(out_sock, "ok", 2);
    }
    else if (packet_type == IOInterface::MessageType::PROCESS_DATA) {
        if (driver_state == s_driver_init) {
            if (!default_data) {
                std::cerr << "WARNING: Getting process data from driver with no default set\n";
            }
            else {
                driver_state = s_driver_operational;
            }
        }
        if (ec_ok && default_data) { // only update the domain if default data has been setup
            assert("updateDomain requires cw data and mask" && cw_data && cw_mask);
            ECInterface::instance()->updateDomain(len, cw_data, cw_mask);
#if VERBOSE_DEBUG
            // check if our mask is not the same as the data that cw has
            //
            if (last_data) {
                bool done_header = false;
                uint8_t *upd_data = ECInterface::instance()->getUpdateData();

                for (size_t i = 0; i < len; ++i) {
                    if (cw_data[i] != upd_data[i]) {
                        if (!done_header) {
                            done_header = true;
                            std::cout << "process data diffs at bytes: ";
                        }
                        std::cout << i << " ";
                    }
                }
                if (done_header) {
                    std::cout << "\n";
                }
            }
#endif
        }
        else if (!default_data) {
            MessageLog::instance()->add("ecat_thread: no default data set yet, cannot update domain");
        }
        delete[] cw_mask;
        delete[] cw_data;
        cw_mask = cw_data = nullptr;
        safeSend(out_sock, "ok", 2);
    }
    return true;
}

void EtherCATThread::operator()() {
    pthread_setname_np(pthread_self(), "iod ethercat");
    Statistic *keep_alive_stat = new Statistic("keep alive margin");
    Statistic::add(keep_alive_stat);



    unsigned long freq = ECInterface::FREQUENCY;
    ClockSync clock_sync;

    sync_sock = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REP);
    sync_sock->bind("inproc://ethercat_sync");

    // when clockwork has output to send to EtherCAT it sends it here
    zmq::socket_t out_sock(*MessagingInterface::getContext(), ZMQ_REP);
    out_sock.bind("inproc://ethercat_output");

    uint64_t global_clock = 0;
    bool first_run = true;
    // assume all is ok with EtherCAT. If modules go offline this flips and we stop polling
    static bool ec_ok = true;
    while (!program_done && !waitForSync(*sync_sock)) {
        usleep(100);
    }
    while (!program_done) {
        uint64_t now = nowMicrosecs();
        uint64_t period = 1000000 / freq;
        {
            auto before_sync = std::chrono::steady_clock::now();
            clock_sync();
            auto after_sync = std::chrono::steady_clock::now();
            uint64_t delta = std::chrono::duration_cast<std::chrono::microseconds>(after_sync - before_sync).count();
            auto cycle_time =  get_cycle_time();
            if (delta > cycle_time) {
                DBG_ETHERCAT << "Clock sync overtime: " << delta << " > " << cycle_time;
            }
        }
        int num_updates = 0;

        // machine_was ready becomes true once the machine becomes ready for the
        // first time and stays that way.
        if (machine_is_ready && !machine_was_ready) {
            machine_was_ready = true;
        }

        if (machine_was_ready) {
            if (ec_ok != machine_is_ready) {
                std::cout << "ethercat thread is now " << machine_is_ready << "\n";
                ec_ok = machine_is_ready;
                if (last_data) {
                    delete[] last_data;
                    last_data = 0;
                }
                if (dbg_mask) {
                    delete[] dbg_mask;
                    dbg_mask = 0;
                }
                if (cmp_data) {
                    delete[] cmp_data;
                    cmp_data = 0;
                }
                if (!machine_is_ready) {
                    driver_state = s_driver_init;
                }
            }
        }

        // keep polling ethercat if we are not online but do not
        // exchange clockwork machine state
        next_ecat_receive = microsecs() + period / 2;
        ECInterface::instance()->receiveState();
        global_clock = ECInterface::instance()->getApplicationTime(); //updateClock(global_clock);

        if (machine_is_ready && ECInterface::instance()->data.getProcessMask()) {
            if (status == e_collect) {
                DBG_ETHERCAT_PACKETS << "Asking ECInterface to collect state\n";
                num_updates = ECInterface::instance()->collectState();
                DBG_ETHERCAT_PACKETS << "Num updates from ecat_thread: " << num_updates << "\n";
            }

            // send all process domain data once the domain is operational
            // check keep-alives on the clockwork communications channel
            //   four periods: 4 * period / 1000 = period/250
            bool need_ping =
                keep_alive > 0 && (last_ping + keep_alive - period < now) ? true : false;

            // if we have collected data from EtherCAT, send it to clockwork
            if (status == e_collect && (first_run || num_updates || need_ping)) {
                if (driver_state == s_driver_operational) {
                    first_run = false;
                }
                need_ping = false;
                int stage = sendMultiPart(sync_sock, global_clock);
#if VERBOSE_DEBUG
                if (stage == 5) {
                    DBG_MSG << "send done\n";
                }
#endif
                assert(stage == 5);
                status = e_update; // time to send process data to EtherCAT
            }
            if (status == e_update &&
                getEtherCatResponse(sync_sock, global_clock, keep_alive_stat)) {
                status = e_collect;
            }
        }
        //else
        //    DBG_ETHERCAT << "machine is not ready; no state collected\n";

        // check for communication from clockwork
        if (getClockworkMessage(out_sock, ec_ok) && microsecs() < next_ecat_receive - 300) {
            DBG_ETHERCAT_PACKETS << "ecat thread got clockwork message. next ecat: "
                                 << next_ecat_receive << "\n";
        }

        ECInterface::instance()->sendUpdates();
        checkAndUpdateCycleDelay();
    }
    keep_alive_stat->report(std::cout);
}
