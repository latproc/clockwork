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

#include "daemon.h"
#include "IOComponent.h"
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <zmq.hpp>
#include "Channel.h"

#include <fstream>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <list>
#include <map>
#include <signal.h>
#include <sys/stat.h>
#include <utility>

#ifndef EC_SIMULATOR
#include "tool/MasterDevice.h"
#endif
#ifdef SOEM_ETHERCAT
#include "EtherCATthread.h"
#endif

#define __MAIN__
#include "DebugExtra.h"
#include "Dispatcher.h"
#include "MQTTInterface.h"
#include "MessagingInterface.h"
#include "ModbusInterface.h"
#include "PredicateAction.h"
#include "ProcessingThread.h"
#include "Scheduler.h"
#include "Statistic.h"
#include "Statistics.h"
#include "clockwork.h"
#include "symboltable.h"
#include "export_code.h"
#include <libgen.h>

bool program_done = false;
bool machine_is_ready = false;

void usage(int argc, char *argv[]);
void displaySymbolTable();

Statistics *statistics = NULL;

//static boost::mutex io_mutex;
boost::condition_variable_any ecat_polltime;

typedef std::list<std::string> StringList;
std::map<std::string, StringList> wiring;

namespace {

void finish(int sig) {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGINT, &sa, 0);
    program_done = true;
    exit(0);
}

bool setup_signals() {
    struct sigaction sa;
    sa.sa_handler = finish;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, 0) || sigaction(SIGINT, &sa, 0)) {
        return false;
    }
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, 0)) {
        return false;
    }
    return true;
}

class IODHardwareActivation : public HardwareActivation {
  public:
    void operator()() override {
        DBG_INITIALISATION << "----------- Initialising machines ------------\n";
        initialise_machines();
    }

    bool initialiseHardware() override { return true; }
};

} // namespace

int main(int argc, char const *argv[]) {
    std::string thread_name("cw_main");
    Daemon daemon{argv[0], thread_name};

    zmq::socket_t sim_io(*MessagingInterface::getContext(), ZMQ_REP);
    sim_io.bind("inproc://ethercat_sync");

    std::list<std::string> source_files;
    int load_error = loadOptions(argc, argv, source_files);
    if (load_error) {
        return load_error;
    }

    statistics = new Statistics;
    load_error = loadConfig(source_files);
    if (load_error) {
        Dispatcher::instance()->stop();
        Scheduler::instance()->stop();
        return load_error;
    }
    daemon.do_not_export_internal_properties();

    if (dependency_graph()) {
        display_dependency_graph();
    }

    if (test_only()) {
        return export_modbus_mappings(load_error);
    }

    if (export_to_c()) {
        return generate_c() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    bool sigok = setup_signals();
    assert(sigok);

    IODHardwareActivation iod_activation;
    daemon.start(&iod_activation, daemon.user_data);

    // let channels start processing messages
    Channel::startChannels();

    char buf[100];
    size_t response_len;
    DBG_INITIALISATION << "processing has started\n";
    uint64_t then = microsecs();
    while (!program_done) {
        safeRecv(sim_io, buf, 10, false, response_len, 100);
        uint64_t now = microsecs();

        int64_t delta = now - then;
        // use the clockwork interpreter's current cycle delay
        const Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
        assert(cycle_delay_v);
        int64_t delay = cycle_delay_v->iValue;
        if (delay <= 1000) {
            delay = 1000;
        }
        if (delay - delta > 50) {
            delay = delay - delta - 50;
            //NB_MSG << "waiting: " << delay << "\n";
            struct timespec sleep_time;
            sleep_time.tv_sec = delay / 1000000;
            sleep_time.tv_nsec = (delay * 1000) % 1000000000L;
            int rc;
            struct timespec remaining;
            while ((rc = nanosleep(&sleep_time, &remaining) == -1)) {
                sleep_time = remaining;
            }
        }

        then = now;
    }
    try {
        daemon.stop();
    }
    catch (const zmq::error_t &) {
    }
    return 0;
}
