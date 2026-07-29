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

#include "DebugExtra.h"
#include "Logger.h"
#include "cJSON.h"
#include <assert.h>
#include <exception>
#include <iostream>
#include <map>
#include <math.h>
#include <zmq.hpp>
#ifdef DYNAMIC_VALUES
#include "dynamic_value.h"
#endif
#include "Channel.h"
#include "CommandManager.h"
#include "ConnectionManager.h"
#include "Dispatcher.h"
#include "MessageEncoding.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "anet.h"
#include "options.h"
#include "symboltable.h"

/*  A command manager maintains a connection to the clockwork driver on its
    command socket and passes commands arriving from the program's main
    thread to the remote driver through the command socket.
*/
bool CommandManager::setupConnections() {
    if (setup_status == e_startup) {
        char url[100];
        snprintf(url, 100, "tcp://%s:%d", host_name.c_str(), port);
        setup->connect(url);
        monit_setup->setEndPoint(url);
        setup_status = e_waiting_connect;
        //usleep(5000);
    }
    return false;
}

bool CommandManager::checkConnections() {
    if (monit_setup->disconnected()) {
        setup_status = e_startup;
        setupConnections();
        //usleep(50000);
        return false;
    }
    if (monit_setup->disconnected()) {
        return false;
    }
    return true;
}

bool CommandManager::checkConnections(zmq::pollitem_t *items, int num_items, zmq::socket_t &cmd) {
    int rc = 0;
    if (setup_status != e_waiting_connect && monit_setup->disconnected()) {
        if (setup_status != e_waiting_connect) {
            {
                FileLogger fl(program_name);
                fl.f() << "CommandManagercheckConnections() attempting to setup connection "
                       << " setup status is " << setup_status << "\n"
                       << std::flush;
            }
            setup_status = e_startup;
            setupConnections();
            //usleep(50000);
        }
        else {
            FileLogger fl(program_name);
            fl.f() << "CommandManager has no client or setup connection: setup status is "
                   << setup_status << "\n"
                   << std::flush;
        }

        return false;
    }
    if (monit_setup->disconnected()) {
        rc = zmq::poll(&items[1], num_items - 1, 0);
    }
    else {
        rc = zmq::poll(&items[0], num_items, 0);
    }

    char buf[1000];
    size_t msglen = 0;
    // After a REP recv we must always send a reply before the next recv, or
    // every inproc REQ peer (device_connector status/property path) freezes.
    if (rc > 0 && run_status == e_waiting_cmd && items[1].revents & ZMQ_POLLIN) {
        //      {FileLogger fl(program_name); fl.f() << "command socket activity when waiting cmd.  receiving...\n" << std::flush; }
        safeRecv(cmd, buf, 1000, false, msglen, 1);
        if (msglen) {
            buf[msglen] = 0;
            //{FileLogger fl(program_name); fl.f() << "got cmd: " << buf << " for clockwork\n"<<std::flush; }
            //DBG_MSG << "got cmd: " << buf << "\n";
            if (!monit_setup->disconnected()) {
                try {
                    setup->send(buf, msglen);
                    run_status = e_waiting_response;
                    cmd_request_start = microsecs();
                }
                catch (const zmq::error_t &) {
                    FileLogger fl(program_name);
                    fl.f() << "CommandManager setup send failed ("
                           << zmq_strerror(zmq_errno())
                           << "); nacking cmd\n"
                           << std::flush;
                    try {
                        cmd.send("failed", 5);
                    }
                    catch (...) {
                    }
                    run_status = e_waiting_cmd;
                    cmd_request_start = 0;
                    recreateSetupSocket();
                }
            }
            else {
                // Received on REP but cannot forward — still must reply.
                try {
                    cmd.send("failed", 5);
                }
                catch (...) {
                }
                run_status = e_waiting_cmd;
                cmd_request_start = 0;
            }
        }
        else {
            FileLogger fl(program_name);
            fl.f() << "No Message\n" << std::flush;
        }
    }
    if (run_status == e_waiting_response && monit_setup->disconnected()) {
        {
            FileLogger fl(program_name);
            fl.f() << "disconnected, attempting reconnect\n" << std::flush;
        }
        const char *msg = "disconnected, attempting reconnect";
        try {
            cmd.send(msg, strlen(msg));
        }
        catch (...) {
        }
        run_status = e_waiting_cmd;
        cmd_request_start = 0;
        recreateSetupSocket();
    }
    else if (run_status == e_waiting_response && cmd_request_start &&
             (microsecs() - cmd_request_start) > cmd_response_timeout_us) {
        {
            FileLogger fl(program_name);
            fl.f() << "CommandManager cmd response timed out after "
                   << (microsecs() - cmd_request_start)
                   << "us; nacking inproc\n"
                   << std::flush;
        }
        try {
            cmd.send("timeout", 7);
        }
        catch (...) {
        }
        run_status = e_waiting_cmd;
        cmd_request_start = 0;
        // Half-open REQ after silent remote: only a new socket can send again.
        recreateSetupSocket();
    }
    else if (rc > 0 && run_status == e_waiting_response && items[0].revents & ZMQ_POLLIN) {
        //{FileLogger fl(program_name); fl.f() << "command socket activity.  receiving...\n" << std::flush; }
        if (safeRecv(*setup, buf, 1000, false, msglen, 0)) {
            try {
                if (msglen && msglen < 1000) {
                    cmd.send(buf, msglen);
                }
                else {
                    cmd.send("error", 5);
                }
            }
            catch (...) {
            }
            run_status = e_waiting_cmd;
            cmd_request_start = 0;
        }
    }
    return true;
}

CommandManager::CommandManager(const char *host, int portnum)
    : host_name(host), port(portnum), setup(0), monit_setup(0), setup_monitor_thread(0) {
    setup = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
    int linger = 0;
    setup->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    monit_setup = new SingleConnectionMonitor(*setup);
    init();
}

void CommandManager::recreateSetupSocket() {
    {
        FileLogger fl(program_name);
        fl.f() << "CommandManager recreating setup REQ socket\n" << std::flush;
    }
    std::string endpoint;
    if (monit_setup) {
        endpoint = monit_setup->endPoint();
    }
    if (endpoint.empty() && !host_name.empty() && port > 0) {
        char url[100];
        snprintf(url, 100, "tcp://%s:%d", host_name.c_str(), port);
        endpoint = url;
    }

    if (monit_setup) {
        monit_setup->abort();
    }
    if (setup_monitor_thread) {
        if (setup_monitor_thread->joinable()) {
            setup_monitor_thread->join();
        }
        delete setup_monitor_thread;
        setup_monitor_thread = 0;
    }

    int linger = 0;
    if (setup) {
        try {
            setup->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        }
        catch (const zmq::error_t &) {
        }
        delete setup;
        setup = 0;
    }
    delete monit_setup;
    monit_setup = 0;

    setup = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
    setup->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    monit_setup = new SingleConnectionMonitor(*setup);
    setup_monitor_thread = new boost::thread(boost::ref(*monit_setup));
    setup_status = e_startup;
    cmd_request_start = 0;

    if (!endpoint.empty()) {
        int counter = 50;
        while (counter-- > 0 && !monit_setup->active()) {
            usleep(1000);
        }
        try {
            setup->connect(endpoint.c_str());
            monit_setup->setEndPoint(endpoint.c_str());
            setup_status = e_waiting_connect;
        }
        catch (const zmq::error_t &) {
            setup_status = e_startup;
        }
    }
}

void CommandManager::init() {
    // client
    if (setup_monitor_thread) {
        if (setup_monitor_thread->joinable()) {
            setup_monitor_thread->join();
        }
        delete setup_monitor_thread;
        setup_monitor_thread = 0;
    }
    setup_monitor_thread = new boost::thread(boost::ref(*monit_setup));

    run_status = e_waiting_cmd;
    setup_status = e_startup;
    cmd_request_start = 0;
}
