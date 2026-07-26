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

#include "ECInterface.h"
#include "IOInterface.h"
#include "MessageLog.h"
#include "value.h"
#include <bits/stdint-uintn.h>
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
#define USE_CHRONO 1

#ifdef USE_CHRONO
#include <csignal>
#include <ctime>
#include <chrono>
#elif USE_RTC
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#endif

#define VERBOSE_DEBUG 0

extern bool machine_is_ready;
const char *EtherCATThread::ZMQ_Addr = "inproc://ecat_thread";
static bool machine_was_ready = false;
uint64_t next_ecat_receive = 0;

EtherCATThread::EtherCATThread()
    : status(e_collect), program_done(false), cycle_delay(1000), keep_alive(4000), last_ping(0) {}

void EtherCATThread::setCycleDelay(long new_val) { cycle_delay = new_val; }

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
        assert(false);
    }
    return received;
}

bool EtherCATThread::checkAndUpdateCycleDelay() {
    // SYSTEM.CYCLE_DELAY = EtherCAT bus clock only (µs). Not Clockwork poll rate.
    // FIXME: potentially not thread safe
    const Value *cycle_delay_v = ClockworkInterpreter::instance()
                                     ? ClockworkInterpreter::instance()->cycle_delay
                                     : nullptr;
    unsigned long desired = get_cycle_time();
    if (cycle_delay_v && cycle_delay_v->iValue >= 100) {
        desired = static_cast<unsigned long>(cycle_delay_v->iValue);
    }
    if (cycle_delay != desired) {
        cycle_delay = desired;
        DBG_INITIALISATION << "EtherCAT CYCLE_DELAY -> " << cycle_delay << " us\n";
#ifdef USE_KERNEL_ETHERCAT
        // iod-elc: applyCyclePeriodUs (locked after activate).
        ECInterface::instance()->applyCyclePeriodUs(cycle_delay);
#else
        // Legacy iod / iod_sdo: no applyCyclePeriodUs — update userspace timer only.
        ECInterface::FREQUENCY =
            cycle_delay > 0 ? static_cast<unsigned int>(1000000UL / cycle_delay) : 1;
        set_cycle_time(cycle_delay);
#endif
        return true;
    }
    return false;
}

#if VERBOSE_DEBUG
static void display(uint8_t *p, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        std::cout << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i]
                  << std::dec;
    }
}
#endif

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

#ifdef USE_SIGNALLER
void sync(zmq::socket_t &clock_sync) { waitForSync(clock_sync); }
#else
#ifdef USE_CHRONO

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>

namespace {

// A pipe used by a timer thread; when a signal is recieved, a char is written
// to the pipe. The thread can be stopped by writing a stop command to the pipe.
int timer_pipe_fds[2] = {-1, -1};
char monitor_stop_command = 'q';
timer_t timerid = nullptr;

std::mutex pipe_mtx;
std::condition_variable pipe_cv; // Signalled when the pipe has been written to
std::mutex timer_start_mtx;
std::condition_variable timer_start_cv;
enum TimerStartState { timer_starting, timer_ready, timer_failed };
TimerStartState timer_start_state = timer_starting;
std::atomic<uint64_t> timer_sequence(0);

bool configure_timing_thread(const char *name, int cpu, int priority) {
    if (cpu > 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu, &cpuset);
        int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
        if (rc != 0) {
            std::cerr << "ERROR: unable to set " << name << " affinity to CPU " << cpu
                      << ": " << strerror(rc) << "\n";
            return false;
        }
    }

    if (priority <= 0) {
        std::cerr << "ERROR: no real-time priority configured for " << name << "\n";
        return false;
    }

    int minimum = sched_get_priority_min(SCHED_FIFO);
    int maximum = sched_get_priority_max(SCHED_FIFO);
    if (priority < minimum || priority > maximum) {
        std::cerr << "ERROR: invalid SCHED_FIFO priority " << priority << " for " << name
                  << "; valid range is " << minimum << ".." << maximum << "\n";
        return false;
    }

    struct sched_param params = {};
    params.sched_priority = priority;
    int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &params);
    if (rc != 0) {
        std::cerr << "ERROR: unable to set SCHED_FIFO priority " << priority << " for " << name
                  << ": " << strerror(rc) << "\n";
        return false;
    }

    int actual_policy = 0;
    struct sched_param actual_params = {};
    rc = pthread_getschedparam(pthread_self(), &actual_policy, &actual_params);
    if (rc != 0 || actual_policy != SCHED_FIFO || actual_params.sched_priority != priority) {
        std::cerr << "ERROR: failed to verify real-time scheduling for " << name << "\n";
        return false;
    }

    std::cerr << "Configured " << name << " on CPU " << cpu
              << " with SCHED_FIFO priority " << priority << "\n";
    return true;
}

void report_timer_start(TimerStartState state) {
    {
        std::lock_guard<std::mutex> lock(timer_start_mtx);
        timer_start_state = state;
    }
    timer_start_cv.notify_one();
}

void handle_timer(int sig, siginfo_t *si, void *uc) {
    if (timer_pipe_fds[1] != -1) {
        write(timer_pipe_fds[1], ".", 1);
    }
    //std::cout << "Timer expired at: " << std::chrono::system_clock::now().time_since_epoch().count() << "\n";
}

void send_stop_message() {
    write(timer_pipe_fds[1], &monitor_stop_command, 1);
}

int adjust_timer_frequency(time_t secs, unsigned long ns) {
    struct itimerspec its;

    // Set the timer period and start delay
    its.it_value.tv_sec = secs;
    its.it_value.tv_nsec = ns;
    its.it_interval.tv_sec = secs;
    its.it_interval.tv_nsec = ns;

    if (timer_settime(timerid, 0, &its, nullptr) == -1) {
        perror("timer_settime");
        return 1;
    }

    return 0;
}

bool flush_input(int fd) {
    char buf[100];
    bool found_stop_signal = false;
    while (true) {
        struct pollfd pfd = {fd, POLLIN, 0};
        int ret = poll(&pfd, 1, 0);
        if (ret > 0) {
            ssize_t num_read = read(fd, buf, sizeof(buf));
            for (ssize_t i = 0; i < num_read; ++i) {
                if (buf[i] == monitor_stop_command) {
                    found_stop_signal = true;
                }
            }
        } else {
            break;  // Exit when there's no more data to read
        }
    }
    if (found_stop_signal) {
        send_stop_message();
    }
    return found_stop_signal;
}

void timer_thread_proc() {
    pthread_setname_np(pthread_self(), "iod ecat timer");
    if (!configure_timing_thread("iod ecat timer", cpu_affinity("ethercat"),
                                 thread_rt_priority("ethercat_timer"))) {
        report_timer_start(timer_failed);
        return;
    }

    // Configure the timer to send a signal to this thread
    struct sigevent sev = {};
    // Setup signal handler
    struct sigaction sa = {};
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handle_timer;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGRTMIN, &sa, nullptr) == -1) {
        perror("sigaction");
        report_timer_start(timer_failed);
        return;
    }

    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SIGRTMIN;
    sev._sigev_un._tid = syscall(SYS_gettid);  // Get the thread ID
    if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
        perror("timer_create");
        report_timer_start(timer_failed);
        return;
    }

    if (adjust_timer_frequency(1, 0) != 0) {
        timer_delete(timerid);
        timerid = nullptr;
        report_timer_start(timer_failed);
        return;
    }
    report_timer_start(timer_ready);

    char buf;
    while (true) {
        // Flushing the input here avoids a flurry of notifications in the case where
        // this thread was not scheduled but signals continued writing to the pipe.
        if (flush_input(timer_pipe_fds[0])) { break; } // abort if a stop command was found

        read(timer_pipe_fds[0], &buf, 1);  // Block until the signal handler writes to the pipe
        if (buf == monitor_stop_command) { break; }
        timer_sequence.fetch_add(1, std::memory_order_release);
        pipe_cv.notify_one();
    }
    timer_delete(timerid);
    timerid = nullptr;
}


} //namespace

void sync() {

    // Configure the timer rate if it has changed
    static bool regular_timer_configured = false;
    static long saved_frequency = ECInterface::FREQUENCY;
    if (!regular_timer_configured || saved_frequency != ECInterface::FREQUENCY) {
        unsigned long delay_ns = get_cycle_time() * 1000;
        // Warning: potentially unsafe access to the timer here.
        int config_error = adjust_timer_frequency(0, delay_ns);
        assert("could not configure posix timer" && !config_error);
        saved_frequency = ECInterface::FREQUENCY;
        regular_timer_configured = true;
    }

    // Delay until the next clock tick after 50us
    static uint64_t observed_sequence = timer_sequence.load(std::memory_order_acquire);
    static auto last_notify = microsecs();
    auto now = last_notify;
    while (true) {
        std::unique_lock<std::mutex> lock(pipe_mtx);
        pipe_cv.wait(lock, [] {
            return timer_sequence.load(std::memory_order_acquire) != observed_sequence;
        });
        observed_sequence = timer_sequence.load(std::memory_order_acquire);
        now = microsecs();
        if (now - last_notify < 50) { continue; } // 
        last_notify = now;
        break;
    }
    //std::cout << "sync() returned at: " << std::chrono::system_clock::now().time_since_epoch().count() << "\n";
}

#elif USE_RTC
int open_rtc(int rtc) {
    if (rtc == -1) {
        rtc = open("/dev/rtc", 0);
        if (rtc == -1) {
            perror("open rtc");
            exit(1);
        }
    }
    return rtc;
}

int configure_rtc_freq(int rtc) {
    long freq = ECInterface::FREQUENCY;
    int rc = ioctl(rtc, RTC_IRQP_SET, freq);
    if (rc == -1) {
        perror("set rtc freq");
        exit(1);
    }
    rc = ioctl(rtc, RTC_IRQP_READ, &freq);
    if (rc == -1) {
        perror("ioctl");
        exit(1);
    }
    std::cout << "Real time clock: freq set to : " << freq << "\n";

    rc = ioctl(rtc, RTC_PIE_ON, 0);
    if (rc == -1) {
        perror("enable rtc pie");
        exit(1);
    }
    return freq;
}

void sync() {
    static int rtc = -1;
    static bool rtc_timer_configured = false;
    static long saved_frequency = ECInterface::FREQUENCY;
    if (!rtc_timer_configured || saved_frequency != ECInterface::FREQUENCY) {
        rtc = open_rtc(rtc);
        saved_frequency = configure_rtc_freq(rtc);
        assert("could not configure real time clock" && rtc >= 0);
        rtc_timer_configured = true;
    }
    static uint64_t last_read_sync_check = 0;
    {
        unsigned long val = 0;
        unsigned long period = 1000000 / ECInterface::FREQUENCY;
        uint64_t t = 0;
        while (1) {
#ifdef KEEP_STATS
            static Statistic clock_delay("clock");
#endif
            auto now = microsecs();
            int nread = read(rtc, &val, sizeof(val));
            t = microsecs();
            if (nread < 0) {
                perror("rtc-read");
                exit(1);
            }
            if (last_read_sync_check) {
                if ((t - last_read_sync_check) < 50) {
                    continue;
                }
            }
#ifdef KEEP_STATS
            clock_delay.add(t - now);
            if (clock_delay.getCount() >= 1000) {
                clock_delay.report(std::cout);
                clock_delay.reset();
            }
#endif
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
                if (rtc == -1) {
                    perror("open rtc");
                    exit(1);
                }
            }
            perror("read rtc");
            exit(1);
        }
        else {
            freq = rtc_val;
        }
        if (freq != ECInterface::FREQUENCY) {
            std::cout << "-------------------- adjusting frequency: " << freq << "->"
                      << ECInterface::FREQUENCY << "\n";
            unsigned long saved_freq = freq;
            freq = ECInterface::FREQUENCY;
            rc = ioctl(rtc, RTC_IRQP_SET, freq);
            if (rc == -1) {
                perror("set rtc freq");
                freq = saved_freq; // will retry
            }
            else {
                rc = ioctl(rtc, RTC_IRQP_READ, &rtc_val);
                if (rc != -1) {
                    std::cout << "frequency is now " << rtc_val << "\n";
                }
            }
        }
    }
}
#else
void sync() {
    uint64_t now = nowMicrosecs();
    int64_t delta = now - then;
    int64_t delay = cycle_delay - delta - 100;
    if (delay > 0) {
        struct timespec sleep_time;
        sleep_time.tv_sec = delay / 1000000;
        sleep_time.tv_nsec = (delay * 1000) % 1000000000L;
        int rc;
        struct timespec remaining;
        while ((rc = nanosleep(&sleep_time, &remaining) == -1)) {
            sleep_time = remaining;
        }
    }
    then = microsecs();
}
#endif
#endif

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

uint64_t updateClock(uint64_t global_clock) {
#ifdef USE_DC
    // Legacy and kernel: IOTIME / process clock are DC application time in µs
    // (nanoseconds / 1000). LPC PID code treats IOTIME as microseconds.
#ifdef USE_KERNEL_ETHERCAT
    // Ensure the kernel path has refreshed application_time before stamping.
    // receiveState(pull) already does this; a zero clock means we fell behind.
    if (ECInterface::instance()->getApplicationTimeNs() == 0) {
        ECInterface::instance()->refreshKernelApplicationTime();
    }
#endif
    global_clock = ECInterface::instance()->getApplicationTimeNs() / 1000;
#else
    //global_clock += period;
    global_clock = microsecs();
#endif
    return global_clock;
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
#if VERBOSE_DEBUG
        else if (cw_data && cw_mask) {
            std::cout << "!";
            display(cw_mask, len);
            std::cout << "\n";
            std::cout << "<";
            display(cw_data, len);
            std::cout << "\n";
        }
#endif
        // Kernel path: apply PROCESS_DATA even before defaults (defaults may be
        // empty). Legacy path still requires default_data once.
        if (ec_ok && (default_data
#ifdef USE_KERNEL_ETHERCAT
                      || true
#endif
                      )) {
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
    if (!configure_timing_thread("iod ethercat", cpu_affinity("ethercat"),
                                 thread_rt_priority("ethercat"))) {
        std::cerr << "FATAL: EtherCAT cyclic thread real-time setup failed; stopping IOD\n";
        std::exit(EXIT_FAILURE);
    }
    Statistic *keep_alive_stat = new Statistic("keep alive margin");
    Statistic::add(keep_alive_stat);

    unsigned long freq = ECInterface::FREQUENCY;
#ifdef USE_CHRONO
    if (timer_pipe_fds[0] == -1) {
        if (pipe(timer_pipe_fds) == -1) {
            perror("timer-pipe");
        }
    }

    {
        std::lock_guard<std::mutex> lock(timer_start_mtx);
        timer_start_state = timer_starting;
    }
    std::thread timer_thread(timer_thread_proc);
    {
        std::unique_lock<std::mutex> lock(timer_start_mtx);
        timer_start_cv.wait(lock, [] { return timer_start_state != timer_starting; });
        if (timer_start_state != timer_ready) {
            std::cerr << "FATAL: EtherCAT timer thread startup failed; stopping IOD\n";
            timer_thread.join();
            std::exit(EXIT_FAILURE);
        }
    }
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
    int res = zmq_setsockopt(clock_sync, ZMQ_SUBSCRIBE, "", 0);
    assert(res == 0);
#endif

    uint64_t global_clock = 0;
    bool first_run = true;
    // assume all is ok with EtherCAT. If modules go offline this flips and we stop polling
    static bool ec_ok = true;
    while (!program_done && !waitForSync(*sync_sock)) {
        usleep(100);
    }
    while (!program_done) {
#ifdef USE_CHRONO
        sync();
#elif USE_RTC
        sync();
#elif USE_SIGNALLER
        sync(clock_sync);
#else
        sync();
#endif
        uint64_t now = nowMicrosecs();
        ECInterface::instance()->setReferenceTime(now % 0x100000000);
        uint64_t period = 1000000 / freq;
        int num_updates = 0;

        // TBD the following line was missing, preventing the ec_ok logic from working
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

        // Bus (CYCLE_DELAY): kernel RT cycles continuously. Userspace only
        // needs a full input snapshot when Clockwork is due to be fed
        // (POLLING_DELAY); intermediate bus frames are dropped for CW.
        // Outputs: sendUpdates publishes only when the commanded shadow is dirty.
        next_ecat_receive = microsecs() + period / 2;

        // POLLING_DELAY (µs) → how often process data is pushed to Clockwork.
        unsigned long pull_us = get_polling_time();
        if (pull_us < 100) {
            pull_us = 100;
        }
#ifdef USE_KERNEL_ETHERCAT
        static uint64_t last_cw_process_push = 0;
        const bool pull_due =
            first_run || (now - last_cw_process_push >= pull_us);
        // Snapshot only when we will collect for CW (or first run). Status/AL
        // still refresh inside receiveState on their own rate limits.
        ECInterface::instance()->receiveState(pull_due && status == e_collect);
#else
        const bool pull_due = true;
        ECInterface::instance()->receiveState(true);
#endif

        if (machine_is_ready && ECInterface::instance()->data.getProcessMask()) {
            global_clock = updateClock(global_clock);

            // send all process domain data once the domain is operational
            // check keep-alives on the clockwork communications channel
            //   four periods: 4 * period / 1000 = period/250
            bool need_ping =
                keep_alive > 0 && (last_ping + keep_alive - period < now) ? true : false;

            // if we have collected data from EtherCAT, send it to clockwork
            if (status == e_collect && pull_due) {
                DBG_ETHERCAT_PACKETS << "Asking ECInterface to collect state\n";
                // domain1_pd is the latest snapshot from receiveState(true).
                num_updates = ECInterface::instance()->collectState();
                DBG_ETHERCAT_PACKETS << "Num updates from ecat_thread: " << num_updates << "\n";
#ifdef USE_KERNEL_ETHERCAT
                // Advance pull clock even if nothing changed (avoid re-collect
                // every bus cycle until the next POLLING_DELAY window).
                last_cw_process_push = now;
#endif
                // Push only when the domain changed, on first run, or keep-alive.
                // Unchanged images no longer always-push: ANALOG/COUNTER IOTIME
                // advances via sampleRegularPolls() reading the live application
                // clock; POINT IOTIME updates only when a bit actually changes.
                if (first_run || num_updates || need_ping) {
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
                    status = e_update; // wait for CW ack of process data
                }
            }
            if (status == e_update &&
                getEtherCatResponse(sync_sock, global_clock, keep_alive_stat)) {
                status = e_collect;
            }
        }
        //else
        //    DBG_ETHERCAT << "machine is not ready; no state collected\n";

#ifndef USE_KERNEL_ETHERCAT
        // Catch a cyclic response that arrived after the receive at the start
        // of this cycle. This must happen before getClockworkMessage() applies
        // new output values, because domain processing restores the preceding
        // LRW image, including its output bytes.
        // Kernel path: RT task owns receive; late re-snapshot is expensive and
        // only needed after CW output traffic (handled below when a message arrives).
        ECInterface::instance()->receivePendingDomainState();
#endif

        // check for communication from clockwork
        if (getClockworkMessage(out_sock, ec_ok) && microsecs() < next_ecat_receive - 300) {
            DBG_ETHERCAT_PACKETS << "ecat thread got clockwork message. next ecat: "
                                 << next_ecat_receive << "\n";
#ifdef USE_KERNEL_ETHERCAT
            // After CW wrote outputs into the shadow, one late snapshot is enough
            // so domain1_pd reflects inputs before the next collect window.
            ECInterface::instance()->receivePendingDomainState();
#endif
        }

        ECInterface::instance()->sendUpdates();
        checkAndUpdateCycleDelay();
    }
#ifdef USE_CHRONO
    send_stop_message();
    std::lock_guard<std::mutex> lock(pipe_mtx);
    pipe_cv.notify_one();  // Wake up sync if it is waiting
    timer_thread.join();
#endif
    keep_alive_stat->report(std::cout);
}
