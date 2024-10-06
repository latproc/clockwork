
#ifdef __linux__
#define USE_CHRONO 1
#endif

#include "clock_sync.h"
#include <stdint.h>

#ifdef USE_CHRONO
#include <csignal>
#include <ctime>
#include <chrono>
#include <signal.h>
#include <unistd.h>
#include <cassert>
#include "value.h"
#include <thread>
#include "ECInterface.h"
#elif USE_RTC
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#endif

#ifdef USE_SIGNALLER
void sync(zmq::socket_t &clock_sync) { waitForSync(clock_sync); }
#else

#ifdef USE_CHRONO

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <poll.h>
#include <sys/syscall.h>
#include <stdlib.h>

namespace {

// A pipe used by a timer thread; when a signal is recieved, a char is written
// to the pipe. The thread can be stopped by writing a stop command to the pipe.
int timer_pipe_fds[2] = {-1, -1};
char monitor_stop_command = 'q';
timer_t timerid = nullptr;

std::mutex pipe_mtx;
std::condition_variable pipe_cv; // Signalled when the pipe has been written to

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
    // Configure the timer to send a signal to this thread
    struct sigevent sev;
    // Setup signal handler
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handle_timer;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGRTMIN, &sa, nullptr) == -1) {
        perror("sigaction");
        return;
    }

    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SIGRTMIN;
    sev._sigev_un._tid = syscall(SYS_gettid);  // Get the thread ID
    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
        perror("timer_create");
        return;
    }

    adjust_timer_frequency(2, 100);

    char buf;
    while (true) {
        // Flushing the input here avoids a flurry of notifications in the case where
        // this thread was not scheduled but signals continued writing to the pipe.
        if (flush_input(timer_pipe_fds[0])) { break; } // abort if a stop command was found

        read(timer_pipe_fds[0], &buf, 1);  // Block until the signal handler writes to the pipe
        if (buf == monitor_stop_command) { break; }
        std::unique_lock<std::mutex> lock(pipe_mtx);
        pipe_cv.notify_one();
    }
    timer_delete(timerid);
    timerid = nullptr;
}


} //namespace

void clock_sync() {

    // Configure the timer rate if it has changed
    static bool regular_timer_configured = false;
    static long saved_frequency = ECInterface::FREQUENCY;
    if (!regular_timer_configured || saved_frequency != ECInterface::FREQUENCY) {
        unsigned long delay_ms = get_cycle_time();
        unsigned long delay_ns = (delay_ms % 1000000UL) * 1000;
        unsigned long delay_s = delay_ms / 1000000UL;
        // Warning: potentially unsafe access to the timer here.
        int config_error = adjust_timer_frequency(delay_s, delay_ns);
        assert("could not configure posix timer" && !config_error);
        saved_frequency = ECInterface::FREQUENCY;
        regular_timer_configured = true;
    }

    // Delay until the next clock tick after 50us
    static auto last_notify = microsecs();
    auto now = last_notify;
    while (true) {
        std::unique_lock<std::mutex> lock(pipe_mtx);
        pipe_cv.wait(lock);  // Wait for the next signal
        now = microsecs();
        if (std::abs(static_cast<long long>(now - last_notify)) < 50) { continue; } // 
        last_notify = now;
        break;
    }
    //std::cout << "clock_sync() returned at: " << std::chrono::system_clock::now().time_since_epoch().count() << "\n";
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

void clock_sync() {
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
#include "value.h"
#include "options.h"

void clock_sync() {
    static uint64_t then = 0;
    uint64_t now = microsecs();
    int64_t delta = now - then;
    int64_t cycle_delay = get_cycle_time();
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

#ifdef USE_CHRONO
ClockSync::ClockSync() : timer_thread(timer_thread_proc) {
#else
ClockSync::ClockSync() {
#endif
#ifdef USE_CHRONO
    if (timer_pipe_fds[0] == -1) {
        if (pipe(timer_pipe_fds) == -1) {
            perror("timer-pipe");
        }
    }

#endif

#ifdef USE_SIGNALLER
    zmq::socket_t clock_sync(*MessagingInterface::getContext(), ZMQ_SUB);
    clock_sync.connect("tcp://localhost:10241");
    int res = zmq_setsockopt(clock_sync, ZMQ_SUBSCRIBE, "", 0);
    assert(res == 0);
#endif
}

ClockSync::~ClockSync() {
#ifdef USE_CHRONO
    send_stop_message();
    std::lock_guard<std::mutex> lock(pipe_mtx);
    pipe_cv.notify_one();  // Wake up sync if it is waiting
    timer_thread.join();
#endif
}

void ClockSync::operator()() {
    clock_sync();
}
