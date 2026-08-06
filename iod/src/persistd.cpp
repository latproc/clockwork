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

#include "ConnectionManager.h"
#include "Logger.h"
#include "MessageEncoding.h"
#include "MessagingInterface.h"
#include "PersistentStore.h"
#include "SocketMonitor.h"
#include "cJSON.h"
#include "symboltable.h"
#include "value.h"
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <inttypes.h>
#include <iostream>
#include <iterator>
#include <list>
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <utility>
#include <zmq.hpp>

namespace po = boost::program_options;

bool done = false;
zmq::socket_t *cmd_socket = 0;

// PERSISTENT STATUS can be large; still must not block forever if iod is stuck.
// sendMessage(timeout=0) rewrites poll interval only and loops until a reply.
static const int64_t PERSIST_CMD_TIMEOUT_MS = 5000;

static void setSocketLinger0(zmq::socket_t &sock) {
    int linger = 0;
    try {
        sock.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    }
    catch (const zmq::error_t &) {
    }
}

// Send on a ZMQ_REQ and wait up to timeout_ms. On timeout/EFSM the socket is
// unusable until recreated — caller must recover setup via SubscriptionManager.
static bool sendWithDeadline(zmq::socket_t &sock, const std::string &msg, std::string &response,
                             int64_t timeout_ms) {
    try {
        safeSend(sock, msg.c_str(), msg.size());
    }
    catch (const zmq::error_t &) {
        std::cerr << "persistd REQ send failed: " << zmq_strerror(zmq_errno()) << "\n";
        return false;
    }

    const uint64_t deadline = microsecs() + (uint64_t)timeout_ms * 1000ULL;
    while (microsecs() < deadline) {
        int64_t remain_ms = (int64_t)((deadline - microsecs()) / 1000ULL);
        if (remain_ms < 1) {
            remain_ms = 1;
        }
        if (remain_ms > 200) {
            remain_ms = 200;
        }
        try {
            zmq::pollitem_t items[] = {{(void *)sock, 0, ZMQ_POLLIN, 0}};
            int n = zmq::poll(items, 1, (long)remain_ms);
            if (n > 0 && (items[0].revents & ZMQ_POLLIN)) {
                char *buf = nullptr;
                size_t len = 0;
                if (safeRecv(sock, &buf, &len, false, 0)) {
                    response = buf ? buf : "";
                    delete[] buf;
                    return true;
                }
            }
        }
        catch (const zmq::error_t &) {
            if (zmq_errno() == EINTR) {
                continue;
            }
            std::cerr << "persistd REQ recv failed: " << zmq_strerror(zmq_errno()) << "\n";
            return false;
        }
    }
    std::cerr << "persistd REQ timed out after " << timeout_ms << "ms\n";
    return false;
}

static void finish(int sig) {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGINT, &sa, 0);
    done = true;
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
    return true;
}

// Soft-reconnect state. ZMQ_EVENT_DISCONNECTED often does not fire on iod
// restart (zombie TCP / auto-reconnect of setup only), so recovery is driven
// from the main loop via checkConnections + forceFullReconnect. Supervise exit
// is a last-resort fallback if soft reconnect stalls (same pattern as mbmon).
static bool need_refresh = false;
static bool link_up = false;
static bool had_session = false;     // completed CHANNEL + PERSISTENT STATUS once
static bool must_rechannel = false; // drop stale CHANNEL/SUB before next collect

class SetupConnectMonitor : public EventResponder {
  public:
    void operator()(const zmq_event_t &event_, const char *addr_) {
        need_refresh = true;
        // Reconnect of setup/SUB after a live session means peer (iod) likely
        // restarted — old CHANNEL port/authority are invalid even if e_done.
        if (had_session) {
            must_rechannel = true;
            link_up = false;
        }
        std::cerr << "persistd iod connected event (" << (addr_ ? addr_ : "?") << ")\n"
                  << std::flush;
        {
            FileLogger fl(program_name);
            fl.f() << "iod connected event (" << (addr_ ? addr_ : "?")
                   << ") had_session=" << (had_session ? "1" : "0") << "\n"
                   << std::flush;
        }
    }
};

class SetupDisconnectMonitor : public EventResponder {
  public:
    void operator()(const zmq_event_t &event_, const char *addr_) {
        // Do not exit(0) here. Exit-only recovery fails when the event never
        // fires, and a mid-outage restart storm is worse than soft reconnect.
        link_up = false;
        need_refresh = true;
        must_rechannel = true;
        std::cerr << "persistd iod disconnected event (" << (addr_ ? addr_ : "?") << ")\n"
                  << std::flush;
        {
            FileLogger fl(program_name);
            fl.f() << "iod disconnected event (" << (addr_ ? addr_ : "?") << ")\n"
                   << std::flush;
        }
    }
};

std::set<std::string> ignored_properties;

class ExitMessage {
  public:
    ExitMessage(const char *msg) : message(msg) {}
    ~ExitMessage() { std::cout << message << "\n"; }
    std::string message;
};

bool loadActiveData(PersistentStore &store, const char *initial_settings) {
    std::cout << "loading data from clockwork\n";
    ExitMessage em("load data done");
    cJSON *obj = cJSON_Parse(initial_settings);
    if (!obj) {
        return false;
    }
    int num_entries = cJSON_GetArraySize(obj);
    if (num_entries) {
        // persitent data is provided as an array of object,
        // each of which is
        // (string name, string property, Value value)
        for (int i = 0; i < num_entries; ++i) {
            cJSON *item = cJSON_GetArrayItem(obj, i);
            if (item->type == cJSON_Array) {
                cJSON *fld = cJSON_GetArrayItem(item, 0);
                assert(fld->type == cJSON_String);
                Value machine = fld->valuestring;

                fld = cJSON_GetArrayItem(item, 1);
                assert(fld->type == cJSON_String);
                Value property = fld->valuestring;

                if (!ignored_properties.count(property.asString())) {
                    cJSON *json_val = cJSON_GetArrayItem(item, 2);
                    switch (json_val->type) {
                    case cJSON_String:
                        store.insert(machine.asString(), property.asString(),
                                     json_val->valuestring);
                        break;
                    case cJSON_Number:
                        if (json_val->valueNumber.kind == cJSON_Number_int_t) {
                            store.insert(machine.asString(), property.asString(),
                                         (int64_t)json_val->valueNumber.val._int);
                        }
                        else {
                            store.insert(machine.asString(), property.asString(),
                                         json_val->valueNumber.val._double);
                        }
                        break;
                    case cJSON_True:
                        store.insert(machine.asString(), property.asString(), true);
                        break;
                    case cJSON_False:
                        store.insert(machine.asString(), property.asString(), false);
                        break;
                    default:
                        assert(false);
                    }
                }
            }
            else {
                char *node = cJSON_Print(item);
                std::cerr << "item " << i << " is not of the expected format: " << node << "\n";
                free(node);
            }
        }
        return true;
    }
    else {
        return true;
    }
}

// Returns true if status was collected (or empty/ignored after retries exhausted
// without hard failure). Returns false if REQ path is broken — caller should
// forceFullReconnect / back off rather than spinning collect forever.
bool CollectPersistentStatus(PersistentStore &store, SubscriptionManager &subscription_manager) {
    std::string initial_settings;
    int tries = 3;
    do {
        std::cout << "-------- Collecting IO Status ---------\n" << std::flush;
        // setup() may have been recreated after a prior timeout/EFSM.
        cmd_socket = &subscription_manager.setup();
        setSocketLinger0(*cmd_socket);

        bool ok = false;
        if (cmd_socket) {
            ok = sendWithDeadline(*cmd_socket, "PERSISTENT STATUS", initial_settings,
                                  PERSIST_CMD_TIMEOUT_MS);
        }
        if (ok) {
            if (strncasecmp(initial_settings.c_str(), "ignored", strlen("ignored")) != 0) {
                if (loadActiveData(store, initial_settings.c_str())) {
                    store.save();
                }
                return true;
            }
            else {
                sleep(2);
            }
        }
        else {
            // Half-open REQ after silent iod: recreate setup so CHANNEL can recover.
            {
                FileLogger fl(program_name);
                fl.f() << "persistd PERSISTENT STATUS failed; recovering setup REQ\n"
                       << std::flush;
            }
            subscription_manager.resetChannelRequestState(true);
            cmd_socket = &subscription_manager.setup();
            sleep(1);
        }
        if (--tries <= 0) {
            std::cerr << "persistd failed to collect status; soft-reconnect\n" << std::flush;
            {
                FileLogger fl(program_name);
                fl.f() << "failed to collect status; soft-reconnect\n" << std::flush;
            }
            return false;
        }
    } while (!done);
    return false;
}

int main(int argc, const char *argv[]) {
    char *pn = strdup(argv[0]);
    program_name = strdup(basename(pn));
    free(pn);

    zmq::context_t context;
    MessagingInterface::setContext(&context);

    ignored_properties.insert("tab");
    ignored_properties.insert("type");
    ignored_properties.insert("name");
    ignored_properties.insert("image");
    ignored_properties.insert("class");
    ignored_properties.insert("state");
    ignored_properties.insert("export");
    ignored_properties.insert("startup_enabled");
    ignored_properties.insert("NAME");
    ignored_properties.insert("STATE");
    ignored_properties.insert("PERSISTENT");
    ignored_properties.insert("POLLING_DELAY");
    ignored_properties.insert("TRACEABLE");
    ignored_properties.insert("IOTIME");

    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")("verbose", "display changes on stdout");
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 1;
    }

    bool verbose = false;
    if (vm.count("verbose")) {
        verbose = true;
    }

    setup_signals();

    // Line-buffer logs so /tmp/persistd.log shows reconnect progress promptly.
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IOLBF, 0);

    PersistentStore store("persist.dat");
    //store.load(); // disabled load store since we take it from clockwork now

    SubscriptionManager subscription_manager("PERSISTENCE_CHANNEL", eCLOCKWORK);
    SetupConnectMonitor connect_responder;
    SetupDisconnectMonitor disconnect_responder;
    cmd_socket = &subscription_manager.setup();
    // Watch both setup (5555) and subscriber (CHANNEL port). iod restart often
    // kills the dynamic SUB first; setup-only monitoring missed that path.
    subscription_manager.monit_setup->addResponder(ZMQ_EVENT_CONNECTED, &connect_responder);
    subscription_manager.monit_setup->addResponder(ZMQ_EVENT_DISCONNECTED, &disconnect_responder);
    subscription_manager.monit_subs.addResponder(ZMQ_EVENT_CONNECTED, &connect_responder);
    subscription_manager.monit_subs.addResponder(ZMQ_EVENT_DISCONNECTED, &disconnect_responder);

    int iod_down_streak = 0;
    int sub_error_count = 0;
    // Soft reconnect every few seconds while down; supervise restart if still
    // stranded after a long outage (zombie REQ/SUB sessions).
    const int iod_force_reconnect_every = 3; // seconds (matches down-path sleep)
    const int iod_exit_after_seconds = 90;   // long enough for iod boot + CHANNEL
    uint64_t last_zombie_check_us = 0;

    while (!done) {
        zmq::pollitem_t items[] = {
            {(void *)subscription_manager.setup(), 0, ZMQ_POLLERR | ZMQ_POLLIN, 0},
            {(void *)subscription_manager.subscriber(), 0, ZMQ_POLLERR | ZMQ_POLLIN, 0},
        };

        try {
            if (!subscription_manager.checkConnections()) {
                if (link_up) {
                    std::cerr << "persistd: no connection to iod — reconnecting\n" << std::flush;
                    {
                        FileLogger fl(program_name);
                        fl.f() << "no connection to iod — reconnecting\n" << std::flush;
                    }
                }
                link_up = false;
                need_refresh = true;
                ++iod_down_streak;

                // checkConnections already forceFullReconnects on many paths;
                // also poke on a timer so long outages / zombie e_done sessions
                // cannot sit forever without re-CHANNEL. Skip until we have had
                // a live session — otherwise streak==1 aborts the first CHANNEL
                // handshake during cold start.
                if (had_session &&
                    (iod_down_streak == 1 ||
                     (iod_down_streak % iod_force_reconnect_every) == 0)) {
                    subscription_manager.forceFullReconnect("persistd iod link down");
                }

                if (iod_down_streak >= iod_exit_after_seconds) {
                    std::cerr << "persistd: iod reconnect stalled for " << iod_down_streak
                              << "s; exiting for supervise restart\n"
                              << std::flush;
                    {
                        FileLogger fl(program_name);
                        fl.f() << "iod reconnect stalled for " << iod_down_streak
                               << "s; exiting for supervise restart\n"
                               << std::flush;
                    }
                    exit(0);
                }

                usleep(1000000);
                continue;
            }

            iod_down_streak = 0;

            // Full CHANNEL session required before treating the link as up.
            if (subscription_manager.setupStatus() != SubscriptionManager::e_done) {
                usleep(50000);
                continue;
            }

            // Zombie recovery: setup TCP may auto-reconnect to a new iod while
            // CHANNEL grant/SUB stay stale and monitors never flip disconnected.
            const uint64_t now_us = microsecs();
            if (had_session &&
                (now_us - last_zombie_check_us) > 10000000ULL) {
                last_zombie_check_us = now_us;
                if (subscription_manager.monit_setup->disconnected() ||
                    subscription_manager.monit_subs.disconnected()) {
                    std::cerr << "persistd: monitor shows down while e_done — full reconnect\n"
                              << std::flush;
                    link_up = false;
                    need_refresh = true;
                    must_rechannel = true;
                    subscription_manager.forceFullReconnect("persistd monitor down at e_done");
                    usleep(200000);
                    continue;
                }
            }

            // Drop stale CHANNEL/SUB before collecting after peer loss/reconnect.
            // PERSISTENT STATUS on setup alone is not enough — SUB must re-bind.
            if (must_rechannel) {
                std::cerr << "persistd: peer change — full CHANNEL reconnect\n" << std::flush;
                {
                    FileLogger fl(program_name);
                    fl.f() << "peer change — full CHANNEL reconnect\n" << std::flush;
                }
                link_up = false;
                need_refresh = true;
                if (subscription_manager.forceFullReconnect("persistd must_rechannel")) {
                    must_rechannel = false;
                }
                usleep(200000);
                continue;
            }

            if (!link_up || need_refresh) {
                if (!CollectPersistentStatus(store, subscription_manager)) {
                    link_up = false;
                    need_refresh = true;
                    must_rechannel = true;
                    subscription_manager.forceFullReconnect("persistd collect failed");
                    usleep(500000);
                    continue;
                }
                std::cerr << "persistd: iod link up — store refreshed\n" << std::flush;
                {
                    FileLogger fl(program_name);
                    fl.f() << "iod link up — store refreshed\n" << std::flush;
                }
                link_up = true;
                had_session = true;
                need_refresh = false;
                must_rechannel = false;
                last_zombie_check_us = microsecs();
            }
        }
        catch (const zmq::error_t &e) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            std::cerr << "persistd connection poll error: " << e.what() << "\n" << std::flush;
            link_up = false;
            need_refresh = true;
            subscription_manager.forceFullReconnect("persistd poll exception");
            usleep(400000);
            continue;
        }

        int rc = 0;
        try {
            rc = zmq::poll(&items[0], 2, 500);
        }
        catch (const zmq::error_t &e) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            std::cerr << "persistd zmq::poll error: " << e.what() << "\n" << std::flush;
            link_up = false;
            need_refresh = true;
            subscription_manager.forceFullReconnect("persistd zmq::poll exception");
            usleep(400000);
            continue;
        }
        if (rc == 0) {
            continue;
        }
        if (!(items[1].revents & ZMQ_POLLIN)) {
            continue;
        }

        char *data = 0;
        size_t len = 0;
        try {
            MessageHeader mh;
            if (!safeRecv(subscription_manager.subscriber(), &data, &len, false, 1, mh)) {
                // Empty/failed recv: after peer death prefer soft reconnect.
                if (errno != EAGAIN && errno != EINTR) {
                    std::cerr << "persistd subscriber recv failed: " << zmq_strerror(zmq_errno())
                              << "\n"
                              << std::flush;
                    link_up = false;
                    need_refresh = true;
                    subscription_manager.forceFullReconnect("persistd subscriber recv error");
                    if (++sub_error_count > 5) {
                        exit(0); // supervise restart
                    }
                    usleep(200000);
                }
                if (data != nullptr) {
                    delete[] data;
                }
                continue;
            }
            if (!len) {
                if (data != nullptr) {
                    delete[] data;
                }
                continue;
            }
        }
        catch (const zmq::error_t &e) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "error: " << e.what() << " receiving data\n" << std::flush;
            if (data != nullptr) {
                delete[] data;
            }
            link_up = false;
            need_refresh = true;
            subscription_manager.forceFullReconnect("persistd subscriber exception");
            if (++sub_error_count > 5) {
                exit(0);
            }
            usleep(200000);
            continue;
        }
        sub_error_count = 0;

        if (verbose) {
            std::cout << data << "\n";
        }

        try {
            std::string cmd;
            std::list<Value> *param_list = 0;
            if (MessageEncoding::getCommand(data, cmd, &param_list)) {
                if (cmd == "PROPERTY" && param_list && param_list->size() == 3) {
                    std::list<Value>::const_iterator iter = param_list->begin();
                    Value machine_name = *iter++;
                    Value property_name = *iter++;
                    Value value = *iter++;
                    store.insert(machine_name.asString(), property_name.asString(), value);
                    store.save();
                }
                else {
                    std::cerr << "unexpected command: " << cmd << " sent to persistd\n";
                }
            }
            else {
                std::istringstream iss(data);
                std::string property, op, value;
                iss >> property >> op >> value;
                std::string machine_name;
                store.split(machine_name, property);
                store.insert(machine_name, property, value.c_str());
                store.save();
            }
            if (param_list) {
                delete param_list;
                param_list = 0;
            }
        }
        catch (const std::exception &e) {
            std::cerr << "exception " << e.what() << " processing: " << data << "\n";
        }
        delete[] data;
    }

    return 0;
}
