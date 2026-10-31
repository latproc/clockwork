#include "MessageEncoding.h"
#include "value.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <zmq.hpp>

static bool write_text(const std::string &path, const std::string &body) {
    std::ofstream out(path.c_str());
    out << body;
    return out.good();
}

static pid_t spawn_logged(const char *log, const char *bin,
                          std::vector<std::string> args) {
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }
        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(bin));
        for (size_t i = 0; i < args.size(); ++i) {
            argv.push_back(const_cast<char *>(args[i].c_str()));
        }
        argv.push_back(nullptr);
        execv(bin, &argv[0]);
        _exit(127);
    }
    return pid;
}

static void kill_wait(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    kill(pid, SIGTERM);
    usleep(100000);
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

static bool req(zmq::socket_t &socket, const std::string &message,
                std::string &reply, int tries) {
    for (int i = 0; i < tries; ++i) {
        try {
            zmq::message_t out(message.size());
            std::memcpy(out.data(), message.data(), message.size());
            if (!socket.send(out, ZMQ_DONTWAIT)) {
                usleep(50000);
                continue;
            }
            zmq::pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
            if (zmq::poll(items, 1, 2000) > 0 &&
                (items[0].revents & ZMQ_POLLIN)) {
                zmq::message_t in;
                if (socket.recv(&in, 0)) {
                    reply.assign(static_cast<char *>(in.data()), in.size());
                    return true;
                }
            }
        }
        catch (const zmq::error_t &) {
            usleep(50000);
        }
    }
    return false;
}

static zmq::socket_t connect_req(zmq::context_t &context, int port) {
    zmq::socket_t socket(context, ZMQ_REQ);
    int linger = 0;
    socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    char endpoint[64];
    std::snprintf(endpoint, sizeof(endpoint), "tcp://127.0.0.1:%d", port);
    socket.connect(endpoint);
    return socket;
}

static bool cw_command(zmq::socket_t &iod, const char *command,
                       const char *machine, std::string &reply) {
    std::string message = MessageEncoding::encodeCommand(
        "SEND", Value(command), Value("TO", Value::t_string), Value(machine));
    return req(iod, message, reply, 5);
}

static bool wait_property(zmq::socket_t &iod, const char *machine,
                          const char *property, const char *expected,
                          int tries) {
    std::string message =
        MessageEncoding::encodeCommand("GET", Value(machine), Value(property));
    for (int i = 0; i < tries; ++i) {
        std::string reply;
        if (req(iod, message, reply, 2) &&
            reply.find(expected) != std::string::npos &&
            reply.find("Error") == std::string::npos &&
            reply.find("Unknown") == std::string::npos) {
            return true;
        }
        usleep(100000);
    }
    return false;
}

static std::string get_property(zmq::socket_t &iod, const char *machine,
                                const char *property) {
    std::string reply;
    std::string message =
        MessageEncoding::encodeCommand("GET", Value(machine), Value(property));
    req(iod, message, reply, 2);
    return reply;
}

static bool wait_count(zmq::socket_t &iod, const char *machine,
                       const char *command, int expected, int tries) {
    const char *property =
        (std::strcmp(machine, "watcher") == 0 ||
         std::strcmp(machine, "grab_binder") == 0)
            ? "row_count"
            : "status_count";
    for (int i = 0; i < tries; ++i) {
        std::string reply;
        if (cw_command(iod, command, machine, reply) &&
            wait_property(iod, machine, property,
                          std::to_string(expected).c_str(), 1)) {
            return true;
        }
        usleep(100000);
    }
    return false;
}

int main() {
    const char *cw_bin = getenv("CW");
    const char *dbd_bin = getenv("DBD");
    const char *dbsvr_bin = getenv("DBSVR");
    if (!cw_bin || !*cw_bin || !dbd_bin || !*dbd_bin || !dbsvr_bin ||
        !*dbsvr_bin) {
        std::cerr << "CW, DBD and DBSVR must be set\n";
        return 77;
    }

    const int base = 25000 + static_cast<int>(getpid() % 400);
    const int dbsvr_port = base;
    const int notify_port = base + 1;
    const int cw_cmd = base + 2;
    const int cw_pub = base + 3;
    const int cw_ps = base + 4;
    const int cw_mp = base + 5;
    const int db_channel = base + 6;

    char root[128];
    std::snprintf(root, sizeof(root), "/tmp/cw_dbd_watch_%d",
                  static_cast<int>(getpid()));
    mkdir(root, 0755);

    char app[160];
    std::snprintf(app, sizeof(app), "%s/app", root);
    mkdir(app, 0755);

    char main_cw[16384];
    std::snprintf(
        main_cw, sizeof(main_cw),
        "Bale MACHINE TABLE \"bale_instances\" {\n"
        "  OPTION bale_ref \"\" KEY;\n"
        "  OPTION station \"\";\n"
        "  OPTION entered_at \"\";\n"
        "  OPTION updated_at \"\";\n"
        "}\n"
        "Watcher MACHINE {\n"
        "  OPTION response JSON_VALUE {};\n"
        "  OPTION watch_status \"inactive\";\n"
        "  OPTION watch_id \"\";\n"
        "  OPTION watch_generation 0;\n"
        "  OPTION watch_error \"\";\n"
        "  OPTION row_count -1;\n"
        "  rows LIST;\n"
        "  OPTION q JSON_VALUE {\n"
        "    \"action\":\"select\",\"auth\":\"xxx\",\"type\":\"bale_instances\",\n"
        "    \"where\":{\"station\":\"GrabChamber\"},\"order\":[\"entered_at\"],\n"
        "    \"watch\":{\"id\":\"grab_queue\",\"tables\":[\"bale_instances\"],\n"
        "      \"coalesce_ms\":20,\"renew_ms\":5000,\"lease_ms\":15000}\n"
        "  };\n"
        "  COMMAND refresh { QUERY q INTO rows; }\n"
        "  COMMAND count { row_count := SIZE OF rows; }\n"
        "}\n"
        "StatusViewer MACHINE {\n"
        "  OPTION response JSON_VALUE {};\n"
        "  OPTION status_count -1;\n"
        "  statuses LIST;\n"
        "  OPTION q JSON_VALUE {\"action\":\"watch_status\",\"watch_id\":\"grab_queue\"};\n"
        "  COMMAND refresh { QUERY q INTO statuses; }\n"
        "  COMMAND count { status_count := SIZE OF statuses; }\n"
        "}\n"
        "ListViewer MACHINE {\n"
        "  OPTION response JSON_VALUE {};\n"
        "  OPTION status_count -1;\n"
        "  statuses LIST;\n"
        "  OPTION q JSON_VALUE {\"action\":\"watch_list\"};\n"
        "  COMMAND refresh { QUERY q INTO statuses; }\n"
        "  COMMAND count { status_count := SIZE OF statuses; }\n"
        "}\n"
        "Remover MACHINE {\n"
        "  OPTION response JSON_VALUE {};\n"
        "  OPTION status_count -1;\n"
        "  statuses LIST;\n"
        "  OPTION q JSON_VALUE {\"action\":\"watch_remove\",\"watch_id\":\"grab_queue\"};\n"
        "  COMMAND refresh { QUERY q INTO statuses; }\n"
        "  COMMAND count { status_count := SIZE OF statuses; }\n"
        "}\n"
        "GrabInterface MACHINE Obj {\n"
        "  OPTION request JSON_VALUE {\n"
        "    \"action\":\"find\",\"auth\":\"xxx\",\"type\":\"bale_instances\",\n"
        "    \"keys\":{\"bale_ref\":\"\"}\n"
        "  };\n"
        "  COMMAND find {\n"
        "    ITEM ${keys.bale_ref} OF request := Obj.bale_ref;\n"
        "    SEND request TO DATABASE_CHANNEL;\n"
        "  }\n"
        "}\n"
        "GrabBinder MACHINE Obj, Interface {\n"
        "  OPTION response JSON_VALUE {};\n"
        "  OPTION watch_status \"inactive\";\n"
        "  OPTION watch_id \"\";\n"
        "  OPTION watch_generation 0;\n"
        "  OPTION watch_error \"\";\n"
        "  OPTION row_count -1;\n"
        "  rows LIST;\n"
        "  OPTION q JSON_VALUE {\n"
        "    \"action\":\"select\",\"auth\":\"xxx\",\"type\":\"bale_instances\",\n"
        "    \"where\":{\"station\":\"GrabChamber\"},\"order\":[\"entered_at\"],\n"
        "    \"watch\":{\"id\":\"grab_autoload\",\"tables\":[\"bale_instances\"],\n"
        "      \"coalesce_ms\":20,\"renew_ms\":5000,\"lease_ms\":15000}\n"
        "  };\n"
        "  COMMAND refresh { QUERY q INTO rows; }\n"
        "  COMMAND count { row_count := SIZE OF rows; }\n"
        "  RECEIVE response_changed {\n"
        "    rows := response AS LIST;\n"
        "    row_count := SIZE OF rows;\n"
        "    IF (row_count > 0) {\n"
        "      head := TAKE FIRST FROM rows;\n"
        "      Obj.bale_ref := ITEM ${bale_ref} OF head AS STRING;\n"
        "      Obj.station := ITEM ${station} OF head AS STRING;\n"
        "      CALL find ON Interface;\n"
        "    };\n"
        "  }\n"
        "}\n"
        "watcher Watcher;\n"
        "status_view StatusViewer;\n"
        "list_view ListViewer;\n"
        "remover Remover;\n"
        "grab_object Bale;\n"
        "grab_interface GrabInterface grab_object;\n"
        "grab_binder GrabBinder grab_object, grab_interface;\n"
        "DATABASE_CHANNEL CHANNEL {\n"
        "  OPTION HOST \"localhost\";\n"
        "  OPTION port %d;\n"
        "  THROTTLE 50;\n"
        "  PUBLISHER;\n"
        "  IGNORES STATE_CHANGES, PROPERTY_CHANGES;\n"
        "}\n",
        db_channel);
    std::string main_path = std::string(app) + "/main.cw";
    if (!write_text(main_path, main_cw)) {
        std::cerr << "could not write test CW\n";
        return 1;
    }

    char db_path[160];
    std::snprintf(db_path, sizeof(db_path), "%s/store.db", root);
    char dport[16], nport[16], cport[16], cpub[16], cps[16], cmp[16];
    std::snprintf(dport, sizeof(dport), "%d", dbsvr_port);
    std::snprintf(nport, sizeof(nport), "%d", notify_port);
    std::snprintf(cport, sizeof(cport), "%d", cw_cmd);
    std::snprintf(cpub, sizeof(cpub), "%d", cw_pub);
    std::snprintf(cps, sizeof(cps), "%d", cw_ps);
    std::snprintf(cmp, sizeof(cmp), "%d", cw_mp);
    char dbsvr_ep[64], notify_ep[64];
    std::snprintf(dbsvr_ep, sizeof(dbsvr_ep), "tcp://127.0.0.1:%d",
                  dbsvr_port);
    std::snprintf(notify_ep, sizeof(notify_ep), "tcp://127.0.0.1:%d",
                  notify_port);

    std::string dbsvr_log = std::string(root) + "/dbsvr.log";
    std::string cw_log = std::string(root) + "/cw.log";
    std::string dbd_log = std::string(root) + "/dbd.log";
    pid_t dbsvr = spawn_logged(
        dbsvr_log.c_str(), dbsvr_bin,
        {"--db", db_path, "--port", dport, "--notify-port", nport});
    usleep(250000);

    zmq::context_t context;
    zmq::socket_t db = connect_req(context, dbsvr_port);
    std::string reply;
    const char *create =
        "{\"action\":\"create\",\"auth\":\"xxx\",\"type\":\"bale_instances\","
        "\"schema\":{\"bale_ref\":\"text primary key\",\"station\":\"text\","
        "\"entered_at\":\"text\",\"updated_at\":\"text\"}}";
    if (!req(db, create, reply, 40)) {
        std::cerr << "dbsvr create failed: " << reply << "\n";
        kill_wait(dbsvr);
        return 2;
    }

    pid_t cw = spawn_logged(
        cw_log.c_str(), cw_bin,
        {"-cp", cport, "-p", cpub, "-ps", cps, "-mp", cmp, "--name",
         "clock_watch", "--nostats", app});
    usleep(500000);
    zmq::socket_t iod = connect_req(context, cw_cmd);
    if (!wait_property(iod, "watcher", "watch_status", "inactive", 20)) {
        std::cerr << "test CW did not start\n";
        kill_wait(cw);
        kill_wait(dbsvr);
        return 3;
    }

    pid_t dbd = spawn_logged(
        dbd_log.c_str(), dbd_bin,
        {"--host", "127.0.0.1", "--cwout", cport, "--dbsvr", dbsvr_ep,
         "--notify", notify_ep});
    usleep(700000);

    std::string ignored;
    if (!cw_command(iod, "refresh", "watcher", ignored) ||
        !wait_property(iod, "watcher", "watch_status", "active", 50)) {
        std::cerr << "watch did not become active\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 4;
    }
    if (!wait_count(iod, "status_view", "refresh", 1, 30)) {
        std::cerr << "watch_status query did not return the registration\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 5;
    }
    if (!wait_count(iod, "list_view", "refresh", 1, 30)) {
        std::cerr << "watch_list query did not return the registration\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 5;
    }
    bool binder_command = cw_command(iod, "refresh", "grab_binder", ignored);
    bool binder_active =
        binder_command &&
        wait_property(iod, "grab_binder", "watch_status", "active", 50);
    bool binder_empty =
        binder_active && wait_count(iod, "grab_binder", "count", 0, 50);
    if (!binder_command || !binder_active || !binder_empty) {
        std::cerr << "Grab auto-load watch did not register: " << ignored
                  << " command=" << binder_command
                  << " active=" << binder_active << " empty=" << binder_empty
                  << " status="
                  << get_property(iod, "grab_binder", "watch_status")
                  << " count="
                  << get_property(iod, "grab_binder", "row_count") << "\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 5;
    }

    const char *insert =
        "{\"action\":\"insert\",\"auth\":\"xxx\",\"type\":\"bale_instances\","
        "\"data\":{\"bale_ref\":\"B261101000001\",\"station\":\"CutterChamber\","
        "\"entered_at\":\"1\",\"updated_at\":\"1\"}}";
    if (!req(db, insert, reply, 20) ||
        !wait_count(iod, "watcher", "count", 0, 50) ||
        !wait_count(iod, "grab_binder", "count", 0, 50)) {
        std::cerr << "non-matching insert changed the Grab watch\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 6;
    }

    const char *move =
        "{\"action\":\"update\",\"auth\":\"xxx\",\"type\":\"bale_instances\","
        "\"keys\":{\"bale_ref\":\"B261101000001\"},"
        "\"data\":{\"station\":\"GrabChamber\",\"updated_at\":\"2\"}}";
    if (!req(db, move, reply, 20) ||
        !wait_count(iod, "watcher", "count", 1, 50) ||
        !wait_count(iod, "grab_binder", "count", 1, 50) ||
        !wait_property(iod, "grab_object", "station", "GrabChamber", 50)) {
        std::cerr << "Cutter-to-Grab move did not auto-bind BALEDETAIL\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 7;
    }

    const char *move_on =
        "{\"action\":\"update\",\"auth\":\"xxx\",\"type\":\"bale_instances\","
        "\"keys\":{\"bale_ref\":\"B261101000001\"},"
        "\"data\":{\"station\":\"CoreChamber\",\"updated_at\":\"3\"}}";
    if (!req(db, move_on, reply, 20) ||
        !wait_count(iod, "watcher", "count", 0, 50) ||
        !wait_count(iod, "grab_binder", "count", 0, 50)) {
        std::cerr << "station move out did not refresh the watch\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 8;
    }

    // A dbd restart loses its in-memory registry. Reissuing the watched query is
    // the client-side recovery primitive; the replacement dbd must accept it
    // and resume notifications.
    kill_wait(dbd);
    dbd = spawn_logged(
        dbd_log.c_str(), dbd_bin,
        {"--host", "127.0.0.1", "--cwout", cport, "--dbsvr", dbsvr_ep,
         "--notify", notify_ep});
    usleep(700000);
    if (!cw_command(iod, "refresh", "watcher", ignored) ||
        !wait_property(iod, "watcher", "watch_status", "active", 50)) {
        std::cerr << "watch did not re-register after dbd restart\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 8;
    }
    const char *insert_again =
        "{\"action\":\"insert\",\"auth\":\"xxx\",\"type\":\"bale_instances\","
        "\"data\":{\"bale_ref\":\"B261101000002\",\"station\":\"GrabChamber\","
        "\"entered_at\":\"3\",\"updated_at\":\"3\"}}";
    if (!req(db, insert_again, reply, 20) ||
        !wait_count(iod, "watcher", "count", 1, 50)) {
        std::cerr << "restarted watch did not receive later changes\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 9;
    }

    // dbsvr restart: the dbd registry survives, but notifications can be
    // missed. Reissuing the watched query reconciles from the durable table.
    kill_wait(dbsvr);
    dbsvr = spawn_logged(
        dbsvr_log.c_str(), dbsvr_bin,
        {"--db", db_path, "--port", dport, "--notify-port", nport});
    usleep(500000);
    if (!cw_command(iod, "refresh", "watcher", ignored) ||
        !wait_property(iod, "watcher", "watch_status", "active", 50) ||
        !wait_count(iod, "watcher", "count", 1, 50)) {
        std::cerr << "watch did not reconcile after dbsvr restart\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 10;
    }

    if (!cw_command(iod, "refresh", "remover", ignored) ||
        !wait_count(iod, "remover", "count", 1, 30) ||
        !wait_property(iod, "watcher", "watch_status", "removed", 30)) {
        std::cerr << "watch_remove did not remove or report the watch\n";
        kill_wait(dbd);
        kill_wait(cw);
        kill_wait(dbsvr);
        return 11;
    }

    kill_wait(dbd);
    kill_wait(cw);
    kill_wait(dbsvr);
    unlink(db_path);
    std::string wal = std::string(db_path) + "-wal";
    std::string shm = std::string(db_path) + "-shm";
    unlink(wal.c_str());
    unlink(shm.c_str());
    std::cout << "ok\n";
    return 0;
}
