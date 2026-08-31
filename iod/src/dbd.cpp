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
#include <vector>
#include <stdio.h>
#include <string.h>
#include <utility>
#include <zmq.hpp>

#include "ConnectionManager.h"
#include "DeadlineReq.h"
#include "DbNotify.h"
#include "MessageEncoding.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <libgen.h>
#include <string>

namespace po = boost::program_options;

int debug = 0;

const char *local_commands = "inproc://local_cmds";

enum ProgramState { s_initialising, s_pausing, s_paused, s_resuming, s_starting, s_running, s_finished } program_state = s_initialising;

int saved_debug = 0;

const int DEBUG_LIB = 1 << 15;
const int DEBUG_ALL = 0xffff;
const int NOTLIB = DEBUG_ALL ^ DEBUG_LIB;

#define DEBUG_BASIC (debug & NOTLIB)

static unsigned long start_t = 0;
static const int64_t DBSVR_TIMEOUT_MS = 5000;
static const int64_t IOD_CMD_TIMEOUT_MS = 3000;

DeadlineReq *g_iod_req = 0;
DeadlineReq *g_dbsvr_req = 0;

std::ostream &timestamp(std::ostream &out) {
    unsigned long t = microsecs() - start_t;
    return out << (t / 1000);
}

static bool sendIodCommand(const std::string &cmd, std::string &reply) {
    if (g_iod_req) {
        return g_iod_req->request(cmd, reply, IOD_CMD_TIMEOUT_MS);
    }
    return false;
}

static void send_response_to_clockwork(cJSON *json_request, const char *buf) {
    if (!json_request || !buf) {
        return;
    }
    struct ResponseTarget {
        Value machine{"manager"};
        Value property{"response"};
    };
    ResponseTarget target;
    auto respond_to = cJSON_GetObjectItem(json_request, "respond_to");
    if (respond_to && respond_to->type == cJSON_String && respond_to->valuestring) {
        std::string respond_to_str = respond_to->valuestring;
        size_t dot = respond_to_str.rfind('.');
        if (dot != std::string::npos) {
            target.machine = Value{respond_to_str.substr(0, dot)};
            target.property = Value{respond_to_str.substr(dot + 1)};
        }
        else {
            // No dot: treat the whole string as the machine name; property
            // stays the default "response".
            target.machine = Value{respond_to_str};
        }
    }
    cJSON *msg = cJSON_Parse(buf);
    if (!msg) {
        std::cout << "could not parse database response: " << buf << "\n";
        return;
    }
    char *str = cJSON_PrintUnformatted(msg);
    auto cmd = MessageEncoding::encodeCommand("PROPERTY", target.machine, target.property,
                                              Value(str ? str : "", Value::t_string));
    free(str);
    cJSON_Delete(msg);
    if (cmd.empty()) {
        return;
    }
    if (debug) {
        std::cout << "sending response to clockwork: " << cmd << "\n" << std::flush;
    }
    std::string response;
    if (sendIodCommand(cmd, response)) {
        std::cout << "response: " << response << "\n";
        auto update = MessageEncoding::encodeCommand("SEND", target.property + Value{"_changed"},
                                                     Value{"TO", Value::t_string}, target.machine);
        if (!update.empty()) {
            sendIodCommand(update, response);
        }
    }
    else {
        std::cout << "send failed\n";
    }
}

static void apply_rows_to_records(cJSON *request, cJSON *reply) {
    if (!request || !reply) {
        return;
    }
    cJSON *type_json = cJSON_GetObjectItem(request, "type");
    const char *type = (type_json && type_json->type == cJSON_String) ? type_json->valuestring : 0;
    cJSON *response = cJSON_GetObjectItem(reply, "response");
    if (!type || !response) {
        return;
    }
    cJSON *keys = cJSON_GetObjectItem(request, "keys");
    cJSON *action_json = cJSON_GetObjectItem(request, "action");
    const char *action =
        (action_json && action_json->type == cJSON_String) ? action_json->valuestring : "";
    if (action && strcmp(action, "delete") == 0) {
        // The reply carries the rows actually deleted. Remove each by its own
        // key; a delete-all (empty keys, no rows) clears every held instance.
        bool removed_any = false;
        auto send_remove = [&](cJSON *row) {
            char *row_s = row ? cJSON_PrintUnformatted(row) : strdup("{}");
            if (row_s) {
                auto cmd = MessageEncoding::encodeCommand("RECORD",
                                                          Value("REMOVE", Value::t_string),
                                                          Value(type, Value::t_string),
                                                          Value(row_s, Value::t_string));
                std::string reply_s;
                sendIodCommand(cmd, reply_s);
                removed_any = true;
            }
            free(row_s);
        };
        if (response->type == cJSON_Array) {
            cJSON *row = response->child;
            while (row) {
                send_remove(row);
                row = row->next;
            }
        }
        else if (response->type == cJSON_Object) {
            send_remove(response);
        }
        if (!removed_any && (!keys || !keys->child)) {
            send_remove(0);
        }
        return;
    }
    auto send_apply = [&](cJSON *row) {
        if (!row) {
            return;
        }
        char *keys_s = keys ? cJSON_PrintUnformatted(keys) : strdup("{}");
        char *row_s = cJSON_PrintUnformatted(row);
        if (keys_s && row_s) {
            auto cmd = MessageEncoding::encodeCommand("RECORD",
                                                      Value("APPLY", Value::t_string),
                                                      Value(type, Value::t_string),
                                                      Value(keys_s, Value::t_string),
                                                      Value(row_s, Value::t_string));
            std::string reply_s;
            sendIodCommand(cmd, reply_s);
        }
        free(keys_s);
        free(row_s);
    };
    if (response->type == cJSON_Array) {
        cJSON *row = response->child;
        while (row) {
            send_apply(row);
            row = row->next;
        }
    }
    else if (response->type == cJSON_Object) {
        send_apply(response);
    }
}

static void apply_notify_payload(const std::string &payload) {
    std::vector<DbNotifyRow> rows;
    parseDbNotify(payload, rows);
    for (size_t i = 0; i < rows.size(); ++i) {
        std::string cmd;
        if (rows[i].action == "delete") {
            const std::string &k =
                rows[i].row_json.empty() ? rows[i].keys_json : rows[i].row_json;
            cmd = MessageEncoding::encodeCommand("RECORD",
                                                 Value("REMOVE", Value::t_string),
                                                 Value(rows[i].type, Value::t_string),
                                                 Value(k, Value::t_string));
        }
        else {
            cmd = MessageEncoding::encodeCommand("RECORD",
                                                 Value("APPLY", Value::t_string),
                                                 Value(rows[i].type, Value::t_string),
                                                 Value(rows[i].keys_json, Value::t_string),
                                                 Value(rows[i].row_json, Value::t_string));
        }
        std::string reply_s;
        sendIodCommand(cmd, reply_s);
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
    debug = debug ? 0 : DEBUG_ALL;
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
    if (sigaction(SIGUSR1, &sa, 0)) {
        return false;
    }
    sa.sa_handler = toggle_debug_all;
    if (sigaction(SIGUSR2, &sa, 0)) {
        return false;
    }
    return true;
}

size_t parseIncomingMessage(const char *data, std::vector<Value> &params) {
    size_t count = 0;
    std::list<Value> parts;
    std::string ds;
    std::list<Value> *param_list = 0;
    if (MessageEncoding::getCommand(data, ds, &param_list)) {
        params.push_back(Value{ds});
        if (param_list) {
            std::list<Value>::const_iterator iter = param_list->begin();
            while (iter != param_list->end()) {
                params.push_back(*iter++);
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
    void operator()(const zmq_event_t &event_, const char *addr_) { NB_MSG << "IOD disconnected\n"; }
};

static bool need_refresh = false;

class SetupConnectMonitor : public EventResponder {
  public:
    void operator()(const zmq_event_t &event_, const char *addr_) { need_refresh = true; }
};

int main(int argc, const char *argv[]) {
    char *pn = strdup(argv[0]);
    program_name = strdup(basename(pn));
    free(pn);

    zmq::context_t context;
    MessagingInterface::setContext(&context);

    start_t = microsecs();
    int cw_port = 5555;
    std::string hostname = "localhost";
    std::string dbsvr_endpoint = "tcp://127.0.0.1:5554";
    std::string notify_endpoint = "tcp://127.0.0.1:5556";

    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")(
        "debug", po::value<int>(&debug)->default_value(0), "set debug level")(
        "host", po::value<std::string>(&hostname)->default_value("localhost"),
        "remote host (localhost)")("cwout", po::value<int>(&cw_port)->default_value(5555),
                                   "clockwork outgoing port (5555)")(
        "dbsvr", po::value<std::string>(&dbsvr_endpoint)->default_value("tcp://127.0.0.1:5554"),
        "datastore REQ endpoint")(
        "notify", po::value<std::string>(&notify_endpoint)->default_value("tcp://127.0.0.1:5556"),
        "datastore PUB notify endpoint")("cwin", "clockwork incoming port (deprecated)");
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 1;
    }
    std::string host("127.0.0.1");
    if (vm.count("cwout")) {
        cw_port = vm["cwout"].as<int>();
    }
    if (vm.count("host")) {
        host = vm["host"].as<std::string>();
    }

    std::cout << "-------- Starting Command Interface ---------\n" << std::flush;
    std::cout << "connecting to clockwork on " << host << ":" << cw_port << "\n";
    std::cout << "dbsvr " << dbsvr_endpoint << " notify " << notify_endpoint << "\n";
    std::ostringstream iod_ep;
    iod_ep << "tcp://" << host << ":" << cw_port;
    DeadlineReq iod_req(context, iod_ep.str());
    DeadlineReq dbsvr_req(context, dbsvr_endpoint);
    g_iod_req = &iod_req;
    g_dbsvr_req = &dbsvr_req;

    zmq::socket_t notify_sub(context, ZMQ_SUB);
    int linger = 0;
    notify_sub.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    notify_sub.setsockopt(ZMQ_SUBSCRIBE, "", 0);
    notify_sub.connect(notify_endpoint.c_str());

    program_state = s_running;
    setup_signals();

    zmq::socket_t iosh_cmd(*MessagingInterface::getContext(), ZMQ_REP);
    iosh_cmd.bind(local_commands);

    SubscriptionManager subscription_manager("DATABASE_CHANNEL", eCLOCKWORK, "localhost", 5555);
    subscription_manager.configureSetupConnection(host.c_str(), cw_port);
    SetupDisconnectMonitor disconnect_responder;
    SetupConnectMonitor connect_responder;
    subscription_manager.monit_setup->addResponder(ZMQ_EVENT_DISCONNECTED, &disconnect_responder);
    subscription_manager.monit_setup->addResponder(ZMQ_EVENT_CONNECTED, &connect_responder);
    subscription_manager.monit_subs.addResponder(ZMQ_EVENT_DISCONNECTED, &disconnect_responder);
    subscription_manager.monit_subs.addResponder(ZMQ_EVENT_CONNECTED, &connect_responder);
    subscription_manager.setupConnections();

    try {
        int exception_count = 0;
        int error_count = 0;
        while (program_state != s_finished) {
            /* SubscriptionManager::checkConnections assumes the command
               socket is last (command_item = num_items - 1). Do not put
               notify_sub after iosh_cmd. */
            zmq::pollitem_t items[] = {
                {(void *)subscription_manager.setup(), 0, ZMQ_POLLIN, 0},
                {(void *)subscription_manager.subscriber(), 0, ZMQ_POLLIN, 0},
                {(void *)notify_sub, 0, ZMQ_POLLIN, 0},
                {iosh_cmd, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0},
            };
            bool channel_up = false;
            try {
                channel_up = subscription_manager.checkConnections(items, 4, iosh_cmd);
                exception_count = 0;
                error_count = 0;
            }
            catch (const zmq::error_t &zex) {
                if (zmq_errno() == EINTR) {
                    continue;
                }
                {
                    FileLogger fl(program_name);
                    fl.f() << "zmq exception " << zmq_errno() << " " << zmq_strerror(zmq_errno())
                           << " polling connections\n";
                }
                subscription_manager.forceFullReconnect("dbd poll error");
                usleep(200000);
                if (++exception_count > 20) {
                    exception_count = 0;
                }
                continue;
            }
            catch (const std::exception &ex) {
                std::cout << "polling connections: " << ex.what() << "\n";
                if (errno == EINTR) {
                    continue;
                }
                subscription_manager.forceFullReconnect("dbd poll std exception");
                usleep(200000);
                continue;
            }
            if (need_refresh && subscription_manager.setupStatus() == SubscriptionManager::e_done) {
                need_refresh = false;
            }

            /* CHANNEL handshake only polls iosh_cmd while setup/subs are down.
               Drain dbsvr notify anyway so both iods apply COMMIT (Q6). */
            {
                zmq::pollitem_t nitem[] = {{(void *)notify_sub, 0, ZMQ_POLLIN, 0}};
                try {
                    zmq::poll(nitem, 1, channel_up ? 0 : 5);
                }
                catch (const zmq::error_t &) {
                }
                if ((channel_up && (items[2].revents & ZMQ_POLLIN)) ||
                    (nitem[0].revents & ZMQ_POLLIN)) {
                    zmq::message_t note;
                    while (notify_sub.recv(&note, ZMQ_DONTWAIT)) {
                        std::string payload(static_cast<char *>(note.data()), note.size());
                        apply_notify_payload(payload);
                    }
                }
            }

            if (!channel_up) {
                usleep(10000);
                continue;
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
                if (errno == EFSM || errno == ENOTSOCK) {
                    subscription_manager.forceFullReconnect("dbd subscriber EFSM");
                    usleep(200000);
                    continue;
                }
                std::cerr << "subscriber recv: " << zmq_strerror(zmq_errno()) << "\n";
                if (++error_count > 5) {
                    subscription_manager.forceFullReconnect("dbd subscriber errors");
                    error_count = 0;
                }
                continue;
            }
            error_count = 0;

            long len = update.size();
            std::vector<char> data(len + 1);
            memcpy(&data[0], update.data(), len);
            data[len] = 0;
            if (DEBUG_BASIC) {
                std::cout << "received: " << &data[0] << " from clockwork\n";
            }

            std::vector<Value> params;
            size_t count = parseIncomingMessage(&data[0], params);
            if (count == 0) {
                std::cout << "received unknown data from clockwork of length " << len << "\n";
            }

            std::string cmd = params.empty() ? "" : params[0].asString();
            if (cmd == "STARTUP") {
                FileLogger fl(program_name);
                fl.f() << " iod startup detected. reconnecting CHANNEL\n";
                subscription_manager.forceFullReconnect("iod STARTUP");
                iod_req.reconnect();
                continue;
            }
            if (cmd == "DEBUG" && params.size() >= 2 &&
                (params[1].kind == Value::t_symbol || params[1].kind == Value::t_string)) {
                debug = params[1].sValue == "ON";
                continue;
            }

            cJSON *json_request = cJSON_Parse(&data[0]);
            if (!json_request) {
                continue;
            }
            if (DEBUG_BASIC) {
                std::cout << "sending: " << &data[0] << "\n";
            }
            std::string dbsvr_reply;
            if (!dbsvr_req.request(&data[0], dbsvr_reply, DBSVR_TIMEOUT_MS)) {
                std::cerr << "dbsvr request failed; will retry with a new socket\n";
                // iod-15: report the failure back to Clockwork rather than
                // silently dropping it, so the author's ON ERROR / CATCH /
                // WAITFOR-timeout path can observe it instead of hanging
                // (see RECORD_DB.md Q11).
                static const char *error_reply =
                    "{\"status\":1,\"request\":\"\",\"response\":\"dbsvr request failed (timeout)\"}";
                send_response_to_clockwork(json_request, error_reply);
                cJSON_Delete(json_request);
                continue;
            }
            if (DEBUG_BASIC) {
                std::cout << dbsvr_reply << "\n";
            }
            send_response_to_clockwork(json_request, dbsvr_reply.c_str());
            cJSON *parsed_reply = cJSON_Parse(dbsvr_reply.c_str());
            apply_rows_to_records(json_request, parsed_reply);
            cJSON_Delete(parsed_reply);
            cJSON_Delete(json_request);
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
        return 1;
    }

    g_iod_req = 0;
    g_dbsvr_req = 0;
    return 0;
}
