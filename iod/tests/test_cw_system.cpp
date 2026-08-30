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

static pid_t spawn_logged(const char *log, const char *bin, std::vector<std::string> args) {
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
        argv.push_back(0);
        execv(bin, &argv[0]);
        _exit(127);
    }
    return pid;
}

static void kill_all(const std::vector<pid_t> &pids) {
    for (size_t i = 0; i < pids.size(); ++i) {
        if (pids[i] > 0) {
            kill(pids[i], SIGTERM);
        }
    }
    usleep(150000);
    for (size_t i = 0; i < pids.size(); ++i) {
        if (pids[i] > 0) {
            kill(pids[i], SIGKILL);
            waitpid(pids[i], 0, 0);
        }
    }
}

static bool req(zmq::socket_t &s, const std::string &json, std::string &reply, int tries) {
    for (int i = 0; i < tries; ++i) {
        try {
            zmq::message_t m(json.size());
            memcpy(m.data(), json.data(), json.size());
            if (!s.send(m, ZMQ_DONTWAIT)) {
                usleep(50000);
                continue;
            }
            zmq::pollitem_t items[] = {{s, 0, ZMQ_POLLIN, 0}};
            if (zmq::poll(items, 1, 500) > 0 && (items[0].revents & ZMQ_POLLIN)) {
                zmq::message_t r;
                if (s.recv(&r, 0)) {
                    reply.assign(static_cast<char *>(r.data()), r.size());
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

static zmq::socket_t connect_req(zmq::context_t &ctx, int port) {
    zmq::socket_t s(ctx, ZMQ_REQ);
    int linger = 0;
    s.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    char ep[64];
    snprintf(ep, sizeof(ep), "tcp://127.0.0.1:%d", port);
    s.connect(ep);
    return s;
}

static bool cw_cmd(zmq::socket_t &s, const std::string &cmd, std::string &reply, int tries) {
    return req(s, cmd, reply, tries);
}

static bool wait_get(zmq::socket_t &s, const char *machine, const char *prop,
                     const char *want, int tries) {
    std::string msg = MessageEncoding::encodeCommand("GET", Value(machine), Value(prop));
    for (int i = 0; i < tries; ++i) {
        std::string reply;
        if (cw_cmd(s, msg, reply, 2) && reply.find(want) != std::string::npos &&
            reply.find("Error") == std::string::npos &&
            reply.find("Unknown") == std::string::npos) {
            return true;
        }
        usleep(100000);
    }
    return false;
}

static bool wait_name_not(zmq::socket_t &s, const char *machine, const char *notwant, int tries) {
    std::string msg = MessageEncoding::encodeCommand("GET", Value(machine), Value("name"));
    for (int i = 0; i < tries; ++i) {
        std::string reply;
        if (cw_cmd(s, msg, reply, 2) && reply.find(notwant) == std::string::npos &&
            reply.find("Error") == std::string::npos &&
            reply.find("Unknown") == std::string::npos) {
            return true;
        }
        usleep(100000);
    }
    return false;
}

int main() {
    const char *cwbin = getenv("CW");
    const char *dbd = getenv("DBD");
    const char *dbsvr = getenv("DBSVR");
    if (!cwbin || !*cwbin || !dbd || !*dbd || !dbsvr || !*dbsvr) {
        std::cerr << "CW, DBD, and DBSVR must be set\n";
        return 77;
    }

    const int base = 22000 + static_cast<int>(getpid() % 400);
    const int dport = base;
    const int nport = base + 1;
    const int cmd_a = base + 2;
    const int pub_a = base + 3;
    const int ps_a = base + 4;
    const int mp_a = base + 5;
    const int cmd_b = base + 6;
    const int pub_b = base + 7;
    const int ps_b = base + 8;
    const int mp_b = base + 9;
    const int ch_a = base + 10;
    const int ch_b = base + 11;

    char root[128];
    snprintf(root, sizeof(root), "/tmp/cw_system_%d", static_cast<int>(getpid()));
    std::string common = std::string(root) + "/common";
    std::string dira = std::string(root) + "/a";
    std::string dirb = std::string(root) + "/b";
    mkdir(root, 0755);
    mkdir(common.c_str(), 0755);
    mkdir(dira.c_str(), 0755);
    mkdir(dirb.c_str(), 0755);

    write_text(common + "/record.cw",
               "Customer RECORD {\n"
               "    OPTION id 0 KEY;\n"
               "    OPTION name \"\";\n"
               "}\n"
               "Ping MACHINE {\n"
               "    idle INITIAL;\n"
               "}\n"
               "PingInterface INTERFACE {\n"
               "    idle INITIAL;\n"
               "}\n"
               "Link CHANNEL {\n"
               "    OPTION PORT 9000;\n"
               "    UPDATES ping_a PingInterface;\n"
               "    UPDATES ping_b PingInterface;\n"
               "}\n"
               "Editor MACHINE {\n"
               "    OPTION q JSON_VALUE {\n"
               "        \"action\": \"insert\", \"auth\": \"xxx\", \"type\": \"customer\",\n"
               "        \"data\": {\"id\": 1, \"name\": \"Ann\"}\n"
               "    };\n"
               "    COMMAND insert {\n"
               "        SEND q TO DATABASE_CHANNEL;\n"
               "    }\n"
               "    OPTION qdel JSON_VALUE {\n"
               "        \"action\": \"delete\", \"auth\": \"xxx\", \"type\": \"customer\",\n"
               "        \"keys\": {\"id\": 1}\n"
               "    };\n"
               "    COMMAND delete {\n"
               "        SEND qdel TO DATABASE_CHANNEL;\n"
               "    }\n"
               "}\n");

    char amain[512];
    snprintf(amain, sizeof(amain),
             "link Link(host: \"127.0.0.1\", port: %d);\n"
             "ping_a Ping;\n"
             "cust Customer;\n"
             "ed Editor;\n"
             "DATABASE_CHANNEL CHANNEL {\n"
             "    OPTION HOST \"localhost\";\n"
             "    OPTION port %d;\n"
             "    THROTTLE 50;\n"
             "    PUBLISHER;\n"
             "    IGNORES STATE_CHANGES, PROPERTY_CHANGES;\n"
             "}\n",
             cmd_b, ch_a);
    write_text(dira + "/main.cw", amain);

    char bmain[512];
    snprintf(bmain, sizeof(bmain),
             "ping_b Ping;\n"
             "cust Customer;\n"
             "ed Editor;\n"
             "DATABASE_CHANNEL CHANNEL {\n"
             "    OPTION HOST \"localhost\";\n"
             "    OPTION port %d;\n"
             "    THROTTLE 50;\n"
             "    PUBLISHER;\n"
             "    IGNORES STATE_CHANGES, PROPERTY_CHANGES;\n"
             "}\n",
             ch_b);
    write_text(dirb + "/main.cw", bmain);

    char db[128], pbuf[16], nbuf[16];
    snprintf(db, sizeof(db), "%s/store.db", root);
    snprintf(pbuf, sizeof(pbuf), "%d", dport);
    snprintf(nbuf, sizeof(nbuf), "%d", nport);
    char dep[64], nep[64];
    snprintf(dep, sizeof(dep), "tcp://127.0.0.1:%d", dport);
    snprintf(nep, sizeof(nep), "tcp://127.0.0.1:%d", nport);

    std::string log_svr = std::string(root) + "/dbsvr.log";
    std::string log_a = std::string(root) + "/cw_a.log";
    std::string log_b = std::string(root) + "/cw_b.log";
    std::string log_da = std::string(root) + "/dbd_a.log";
    std::string log_db = std::string(root) + "/dbd_b.log";

    std::vector<pid_t> pids;
    pid_t svr = spawn_logged(log_svr.c_str(), dbsvr,
                             {"--db", db, "--port", pbuf, "--notify-port", nbuf});
    pids.push_back(svr);
    usleep(200000);

    zmq::context_t ctx;
    zmq::socket_t dbs(ctx, ZMQ_REQ);
    int linger = 0;
    dbs.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    dbs.connect(dep);
    std::string reply;
    const char *create =
        "{\"action\":\"create\",\"auth\":\"xxx\",\"type\":\"customer\","
        "\"schema\":{\"id\":\"integer primary key\",\"name\":\"text\"}}";
    if (!req(dbs, create, reply, 40)) {
        std::cerr << "dbsvr create failed: " << reply << "\n";
        kill_all(pids);
        return 2;
    }

    char cpa[16], ppub_a[16], pps_a[16], pmp_a[16];
    char cpb[16], ppub_b[16], pps_b[16], pmp_b[16];
    snprintf(cpa, sizeof(cpa), "%d", cmd_a);
    snprintf(ppub_a, sizeof(ppub_a), "%d", pub_a);
    snprintf(pps_a, sizeof(pps_a), "%d", ps_a);
    snprintf(pmp_a, sizeof(pmp_a), "%d", mp_a);
    snprintf(cpb, sizeof(cpb), "%d", cmd_b);
    snprintf(ppub_b, sizeof(ppub_b), "%d", pub_b);
    snprintf(pps_b, sizeof(pps_b), "%d", ps_b);
    snprintf(pmp_b, sizeof(pmp_b), "%d", mp_b);

    pid_t cwb = spawn_logged(log_b.c_str(), cwbin,
                             {"-cp", cpb, "-p", ppub_b, "-ps", pps_b, "-mp", pmp_b, "--name",
                              "clock_b", "--nostats", common, dirb});
    pids.push_back(cwb);
    usleep(400000);
    pid_t cwa = spawn_logged(log_a.c_str(), cwbin,
                             {"-cp", cpa, "-p", ppub_a, "-ps", pps_a, "-mp", pmp_a, "--name",
                              "clock_a", "--nostats", common, dira});
    pids.push_back(cwa);

    zmq::socket_t iod_a = connect_req(ctx, cmd_a);
    zmq::socket_t iod_b = connect_req(ctx, cmd_b);
    if (!wait_get(iod_a, "cust", "id", "0", 80) || !wait_get(iod_b, "cust", "id", "0", 80)) {
        std::cerr << "cw did not answer GET cust id\n";
        kill_all(pids);
        return 3;
    }

    char cwa_port[16], cwb_port[16];
    snprintf(cwa_port, sizeof(cwa_port), "%d", cmd_a);
    snprintf(cwb_port, sizeof(cwb_port), "%d", cmd_b);
    pid_t dbd_a = spawn_logged(log_da.c_str(), dbd,
                               {"--host", "127.0.0.1", "--cwout", cwa_port, "--dbsvr", dep,
                                "--notify", nep});
    pid_t dbd_b = spawn_logged(log_db.c_str(), dbd,
                               {"--host", "127.0.0.1", "--cwout", cwb_port, "--dbsvr", dep,
                                "--notify", nep});
    pids.push_back(dbd_a);
    pids.push_back(dbd_b);
    usleep(800000);

    std::string set_id = MessageEncoding::encodeCommand("PROPERTY", Value("cust"), Value("id"),
                                                        Value(static_cast<int64_t>(1)));
    if (!cw_cmd(iod_a, set_id, reply, 10) || !cw_cmd(iod_b, set_id, reply, 10)) {
        std::cerr << "PROPERTY cust id 1 failed: " << reply << "\n";
        kill_all(pids);
        return 4;
    }

    std::string insert =
        MessageEncoding::encodeCommand("SEND", Value("insert"), Value("TO"), Value("ed"));
    if (!cw_cmd(iod_a, insert, reply, 10)) {
        std::cerr << "SEND insert TO ed failed: " << reply << "\n";
        kill_all(pids);
        return 5;
    }

    bool a_ok = wait_get(iod_a, "cust", "name", "Ann", 50);
    bool b_ok = wait_get(iod_b, "cust", "name", "Ann", 50);
    bool link_ok = false;
    {
        std::string msg = MessageEncoding::encodeCommand("GET", Value("ping_b"));
        for (int i = 0; i < 30; ++i) {
            std::string r;
            if (cw_cmd(iod_a, msg, r, 2) && r.find("idle") != std::string::npos) {
                link_ok = true;
                break;
            }
            usleep(100000);
        }
    }
    if (!a_ok || !b_ok) {
        std::cerr << "both cw must show cust.name Ann a=" << a_ok << " b=" << b_ok
                  << " link_shadow=" << link_ok << "\n";
        std::cerr << "logs under " << root << "\n";
        kill_all(pids);
        return 6;
    }
    if (!link_ok) {
        std::cerr << "cw2cw Link shadow ping_b not idle on A (RECORD still matched)\n";
        kill_all(pids);
        return 7;
    }

    // delete path: request + notify propagate RECORD REMOVE to both iods.
    std::string del =
        MessageEncoding::encodeCommand("SEND", Value("delete"), Value("TO"), Value("ed"));
    if (!cw_cmd(iod_a, del, reply, 10)) {
        std::cerr << "SEND delete TO ed failed: " << reply << "\n";
        kill_all(pids);
        return 8;
    }
    bool del_a = wait_name_not(iod_a, "cust", "Ann", 50);
    bool del_b = wait_name_not(iod_b, "cust", "Ann", 50);
    if (!del_a || !del_b) {
        std::cerr << "both cw must reset cust.name after delete a=" << del_a << " b=" << del_b
                  << "\n";
        kill_all(pids);
        return 9;
    }

    kill_all(pids);
    std::cout << "ok\n";
    return 0;
}
