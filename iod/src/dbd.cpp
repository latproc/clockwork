/*
    Copyright (C) 2024 Martin Leadbeater, Michael O'Connor

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

/*
    dbd starts a thread that accepts connections from database server
    programs and starts a subscriber on clockwork to listen for
    changes to items that are exported and for direct database
    commands.
*/

#include <boost/thread/thread.hpp>

#include "DebugExtra.h"
#include "IODCommand.h"
#include "Logger.h"
#include "cJSON.h"
#include "symboltable.h"
#include "value.h"
#include <bitset>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <fstream>
#include <inttypes.h>
#include <iostream>
#include <iterator>
#include <list>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <utility>
#include <zmq.hpp>

#include "ConnectionManager.h"
#include "MessageEncoding.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <string>

namespace {

int debug = 0;

void send_response_to_clockwork(cJSON *json_request, const char *buf) {
    struct ResponseTarget {
        Value machine{"manager"};
        Value property{"response"};
    };
    ResponseTarget target;
    auto respond_to = cJSON_GetObjectItem(json_request, "respond_to");
    if (respond_to && respond_to->type == cJSON_String) {
        std::string respond_to_str = respond_to->valuestring;
        target.machine = Value{respond_to_str.substr(0, respond_to_str.rfind('.'))};
        target.property = Value{respond_to_str.substr(respond_to_str.rfind('.') + 1)};
        std::cout << "responding to " << target.machine << "." << target.property << "\n";
    }
    cJSON *msg = cJSON_Parse(buf);
    if (!msg) {
        std::cout << "could not parse database response: " << buf << "\n";
        return;
    }
    auto str = cJSON_Print(msg);
    assert(str && "cJSON_Print returned a null pointer");
    auto cmd = MessageEncoding::encodeCommand("PROPERTY", target.machine, target.property, Value{msg});
    if (!cmd.empty()) {
        if (debug) {
            std::cout << "sending response to clockwork: " << cmd << "\n" << std::flush;
        }
        std::cout << str << "\n";
        zmq::context_t context;
        zmq::socket_t client(context, ZMQ_REQ);
        client.connect("tcp://127.0.0.1:5555");

        std::string response;
        if (sendMessage(cmd.c_str(), client, response)) {
            std::cout << "response: " << response << "\n";
            auto update = MessageEncoding::encodeCommand("SEND",
                             target.property + Value{"_changed"},
                             Value{"TO",Value::t_string},
                             target.machine);
            if (!update.empty()) {
                if (debug) {
                    std::cout << "sending update to clockwork: " << update << "\n" << std::flush;
                }
                std::string response;
                if (sendMessage(update.c_str(), client, response)) {
                    std::cout << "response: " << response << "\n";
                }
            }
        }
        else {
            std::cout << "send failed\n";
        }
    }
    free(str);
}

}

namespace po = boost::program_options;

static boost::condition_variable_any cond;

const char *local_commands = "inproc://local_cmds";

enum ProgramState { s_initialising, s_pausing, s_paused, s_resuming, s_starting, s_running, s_finished } program_state = s_initialising;

int saved_debug = 0;

const int DEBUG_LIB = 1 << 15;
const int DEBUG_ALL = 0xffff;
const int NOTLIB = DEBUG_ALL ^ DEBUG_LIB;

#define DEBUG_BASIC (debug & NOTLIB)
#define DEBUG_ANY (debug)

std::stringstream dummy;
time_t last = 0;
time_t now;
#define OUT (time(&now) == last) ? dummy : std::cout

static unsigned long start_t = 0;

std::ostream &timestamp(std::ostream &out) {
    unsigned long t = microsecs() - start_t;
    return out << (t / 1000);
}

std::string getIODSyncCommand(int group, int addr, int new_value);

MessagingInterface *g_iodcmd;

char *sendIOD(int group, int addr, int new_value);
char *sendIODMessage(const std::string &s);

std::string getIODSyncCommand(int group, int addr, int new_value) {
    auto msg = MessageEncoding::encodeCommand("MODBUS", Value{group}, Value{addr}, Value{new_value});
    sendIODMessage(msg);

    if (DEBUG_BASIC) {
        std::cout << "IOD command: " << msg << "\n";
    }
    return msg;
}

char *sendIOD(int group, int addr, int new_value) {
    std::string s(getIODSyncCommand(group, addr, new_value));
    if (g_iodcmd) {
        return g_iodcmd->send(s.c_str());
    }
    else {
        if (DEBUG_BASIC) {
            std::cout << "IOD interface not ready\n";
        }
        return strdup("IOD interface not ready\n");
    }
}

char *sendIODMessage(const std::string &s) {
    if (g_iodcmd) {
        return g_iodcmd->send(s.c_str());
    }
    else {
        if (DEBUG_BASIC) {
            std::cout << "IOD interface not ready\n";
        }
        return strdup("IOD interface not ready\n");
    }
}

static void finish(int sig) {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGUSR1, &sa, 0);
    sigaction(SIGUSR2, &sa, 0);
    program_state = s_finished;
}

static void toggle_debug(int sig) {
    if (debug && debug != saved_debug) {
        saved_debug = debug;
    }
    if (debug) {
        debug = 0;
    }
    else {
        if (saved_debug == 0) {
            saved_debug = NOTLIB;
        }
        debug = saved_debug;
    }
}

static void toggle_debug_all(int sig) {
    if (debug) {
        debug = 0;
    }
    else {
        debug = DEBUG_ALL;
    }
}

bool setup_signals() {
    struct sigaction sa;
    sa.sa_handler = finish;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, 0) || sigaction(SIGINT, &sa, 0)) {
        return false;
    }
    sa.sa_handler = toggle_debug;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, 0)) {
        return false;
    }
    sa.sa_handler = toggle_debug_all;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR2, &sa, 0)) {
        return false;
    }
    return true;
}

size_t parseIncomingMessage(const char *data, std::vector<Value> &params) // fillin params
{
    size_t count = 0;
    std::list<Value> parts;
    std::string ds;
    std::list<Value> *param_list = 0;
    if (MessageEncoding::getCommand(data, ds, &param_list)) {
        params.push_back(Value{ds});
        if (param_list) {
            std::list<Value>::const_iterator iter = param_list->begin();
            while (iter != param_list->end()) {
                const Value &v = *iter++;
                params.push_back(v);
            }
        }
        count = params.size();
        delete param_list;
    }
    else {
        std::istringstream iss(data);
        while (iss >> ds) {
            parts.push_back(Value{ds.c_str()});
            ++count;
        }
        std::copy(parts.begin(), parts.end(), std::back_inserter(params));
    }
    return count;
}

class SetupDisconnectMonitor : public EventResponder {
  public:
    void operator()(const zmq_event_t &event_, const char *addr_) {
            NB_MSG << "IOD disconnected\n";
    }
};

static bool need_refresh = false;

class SetupConnectMonitor : public EventResponder {
  public:
    void operator()(const zmq_event_t &event_, const char *addr_) { need_refresh = true; }
};

char *makeRemoteRequest(zmq::socket_t  &client, const char *request) {
  bool sending = true;
  bool got_error;
  do {
    got_error = false;
    if (sending) {
      size_t len = strlen(request);
      zmq::message_t message(len+1);
      memcpy ((void *) message.data(), request, len+1);
      if (client.send(message, ZMQ_DONTWAIT)) {
        sending = false;
      }
      else {
        got_error = true;
      }
    }
    if (!got_error) {
      zmq::message_t message;
      if (client.recv(&message, 0)) {
        sending = true;
        std::cout << "got message of size " << message.size() << "\n";
        size_t len = message.size();
        char *buf = new char[len+1];
        memcpy(buf, message.data(), len);
        buf[len] = 0;
        return buf;
      }
      else {
        got_error = true;
      }
    }
    if (got_error && zmq_errno() == EAGAIN) {
      usleep(1000);
    }
  }
  while (got_error && zmq_errno() == EAGAIN);
  return nullptr;
}

int main(int argc, const char *argv[]) {
    char *pn = strdup(argv[0]);
    program_name = strdup(basename(pn));
    free(pn);

    zmq::context_t context;
    MessagingInterface::setContext(&context);

    start_t = microsecs();
    int cw_port;
    std::string hostname;

    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")(
        "debug", po::value<int>(&debug)->default_value(0),
        "set debug level")("host", po::value<std::string>(&hostname)->default_value("localhost"),
                           "remote host (localhost)")(
        "cwout", po::value<int>(&cw_port)->default_value(5555),
        "clockwork outgoing port (5555)")("cwin", "clockwork incoming port (deprecated)");
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 1;
    }
    int cw_out = 5555;
    std::string host("127.0.0.1");
    // backward compatibility
    if (vm.count("cwout")) {
        cw_out = vm["cwout"].as<int>();
    }
    if (vm.count("host")) {
        host = vm["host"].as<std::string>();
    }
    if (vm.count("debug")) {
        debug = vm["debug"].as<int>();
    }

    if (debug & DEBUG_LIB) {
        //LogState::instance()->insert(DebugExtra::instance()->DEBUG_DATABASE);
    }

    std::cout << "-------- Starting Command Interface ---------\n" << std::flush;
    std::cout << "connecting to clockwork on " << host << ":" << cw_out << "\n";
    g_iodcmd = MessagingInterface::create(host, cw_out);
    g_iodcmd->start();

    program_state = s_running;
    setup_signals();

    // the local command channel accepts commands and relays them to iod.
    zmq::socket_t iosh_cmd(*MessagingInterface::getContext(), ZMQ_REP);
    iosh_cmd.bind(local_commands);

    SubscriptionManager subscription_manager("DATABASE_CHANNEL", eCLOCKWORK, "localhost", 5555);
    subscription_manager.configureSetupConnection(host.c_str(), cw_out);
    SetupDisconnectMonitor disconnect_responder;
    SetupConnectMonitor connect_responder;
    subscription_manager.monit_setup->addResponder(ZMQ_EVENT_DISCONNECTED, &disconnect_responder);
    subscription_manager.monit_setup->addResponder(ZMQ_EVENT_CONNECTED, &connect_responder);
    subscription_manager.setupConnections();

    try {
        int exception_count = 0;
        int error_count = 0;
        while (program_state != s_finished) {

            zmq::pollitem_t items[] = {
                {(void *)subscription_manager.setup(), 0, ZMQ_POLLIN, 0},
                {(void *)subscription_manager.subscriber(), 0, ZMQ_POLLIN, 0},
                { iosh_cmd, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 }
            };
            try {
                if (!subscription_manager.checkConnections(items, 3, iosh_cmd)) {
                    if (debug) {
                        std::cout << "no connection to iod\n";
                    }
                    usleep(10000);
                    exception_count = 0;
                    continue;
                }

                exception_count = 0;
                error_count = 0;
            }
            catch (const zmq::error_t &zex) {
                if (zmq_errno() == EINTR) {
                    continue;
                }
                ++error_count;
                {
                    FileLogger fl(program_name);
                    fl.f() << "zmq exception " << zmq_errno() << " " << zmq_strerror(zmq_errno())
                           << " polling connections\n";
                }
                if (++exception_count <= 5 && program_state != s_finished) {
                    usleep(2000);
                    exit(2);
                    continue;
                }
            }
            catch (const std::exception &ex) {
                std::cout << "polling connections: " << ex.what() << "\n";
                if (errno == EINTR) {
                    continue;
                }
                if (++exception_count <= 5 && program_state != s_finished) {
                    usleep(2000);
                    exit(1);
                    continue;
                }
            }
            if (need_refresh && subscription_manager.setupStatus() == SubscriptionManager::e_done) {
                need_refresh = false;
            }
            if (!(items[1].revents & ZMQ_POLLIN)) {
                usleep(50);
                continue;
            }

            zmq::message_t update;
            if (!subscription_manager.subscriber().recv(&update, ZMQ_DONTWAIT)) {
                if (errno == EAGAIN) {
                    usleep(50);
                    continue;
                }
                if (errno == EFSM) {
                    exit(1);
                }
                if (errno == ENOTSOCK) {
                    exit(1);
                }
                std::cerr << "subscriber recv: " << zmq_strerror(zmq_errno()) << "\n";
                if (++error_count > 5) {
                    exit(1);
                }
                continue;
            }
            error_count = 0;

            long len = update.size();
            char *data = (char *)malloc(len + 1);
            memcpy(data, update.data(), len);
            data[len] = 0;
            if (DEBUG_BASIC) {
                std::cout << "received: " << data << " from clockwork\n";
            }

            // FIXME: Do we need this call to parseIncomingMessage the message here?
            std::vector<Value> params(0);
            size_t count = parseIncomingMessage(data, params);
            if (count == 0) {
                std::cout << "received unknown data, '" << data << "', from clockwork of length "
                          << len << " could not parse\n";
            }
            if (data) {
                try {
                    char *buf = nullptr;
                    cJSON *json_request = nullptr;
                    {
                        zmq::context_t context;
                        zmq::socket_t client(context, ZMQ_REQ);
                        client.connect("tcp://127.0.0.1:5554");
                        std::cout << "sending: " << data << "\n";
                        json_request = cJSON_Parse(data);
                        if (json_request) {
                            buf = makeRemoteRequest(client, data);
                        }

                    }
                    std::cout << buf << "\n";
                    send_response_to_clockwork(json_request, buf);
                    delete[] buf;
                }
                catch(std::exception& e) {
                    std::cerr << "error: " << e.what() << "\n";
                    return 1;
                }
                catch(...) {
                    std::cerr << "Exception of unknown type!\n";
                }

                free(data);
                data = 0;
            }
            std::string cmd(params[0].asString());
            free(data);
            data = 0;

            if (cmd == "STARTUP") {
                {
                    FileLogger fl(program_name);
                    fl.f() << " iod startup detected. restarting\n";
                }
                try {
                }
                catch (const std::exception &ex) {
                    {
                        FileLogger fl(program_name);
                        fl.f() << "exception during restart " << ex.what() << "\n";
                    }
                }
                exit(0);
                break;
            }
            else if (cmd == "DEBUG" && params.size() >= 2 &&
                     (params[1].kind == Value::t_symbol || params[1].kind == Value::t_string)) {
                debug = params[1].sValue == "ON";
            }
        }
    }
    catch (const std::exception &e) {
        if (zmq_errno()) {
            std::cerr << "ZMQ Error: " << zmq_strerror(zmq_errno()) << "\n";
        }
        else {
            std::cerr << "Exception : " << e.what() << "\n";
        }
        return 1;
    }
    catch (...) {
        std::cerr << "Exception of unknown type!\n";
        finish(SIGTERM);
        exit(1);
    }

    return 0;
}
