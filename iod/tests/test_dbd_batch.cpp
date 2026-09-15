// Regression test for dbd batch (array) requests and the --channel option.
//
// Covers two gaps found while pulling rows from an external source:
//   1. dbsvr accepts a JSON array of queries as one atomic batch, but dbd used
//      to treat a message as a single object. This test sends a batch of two
//      selects, each with its own respond_to, and asserts the two replies fan
//      out to two different properties; it also sends a batch of two inserts
//      and asserts both rows committed.
//   2. dbd's subscription channel used to be hardcoded to DATABASE_CHANNEL.
//      A second dbd runs with --channel ALT_CHANNEL and must receive a request
//      published on that channel.
//
// Needs CW, DBD and DBSVR in the environment (see CMakeLists.txt).
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

static bool cw_get(zmq::socket_t &s, const char *machine, const char *prop, std::string &reply) {
    std::string msg = MessageEncoding::encodeCommand("GET", Value(machine), Value(prop));
    return req(s, msg, reply, 4);
}

static bool wait_get(zmq::socket_t &s, const char *machine, const char *prop, const char *want,
                     int tries) {
    for (int i = 0; i < tries; ++i) {
        std::string reply;
        if (cw_get(s, machine, prop, reply) && reply.find(want) != std::string::npos &&
            reply.find("Error") == std::string::npos &&
            reply.find("Unknown") == std::string::npos) {
            return true;
        }
        usleep(100000);
    }
    return false;
}

// Ask dbsvr directly whether a row is present, so the batch pass-through can be
// checked without depending on dbd's reply routing.
static bool dbsvr_has(zmq::socket_t &dbs, const char *name, int tries) {
    char q[256];
    snprintf(q, sizeof(q),
             "{\"action\":\"select\",\"auth\":\"xxx\",\"type\":\"customer\","
             "\"where\":{\"name\":\"%s\"}}",
             name);
    for (int i = 0; i < tries; ++i) {
        std::string reply;
        if (req(dbs, q, reply, 2) && reply.find(name) != std::string::npos) {
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

    const int base = 24000 + static_cast<int>(getpid() % 400);
    const int dport = base;
    const int nport = base + 1;
    const int cmd = base + 2;
    const int pub = base + 3;
    const int ps = base + 4;
    const int mp = base + 5;
    const int ch_default = base + 6;
    const int ch_alt = base + 7;

    char root[128];
    snprintf(root, sizeof(root), "/tmp/cw_dbd_batch_%d", static_cast<int>(getpid()));
    std::string app = std::string(root) + "/app";
    mkdir(root, 0755);
    mkdir(app.c_str(), 0755);

    char main_cw[4096];
    snprintf(main_cw, sizeof(main_cw),
             "Customer RECORD {\n"
             "    OPTION id 0 KEY;\n"
             "    OPTION name \"\";\n"
             "}\n"
             "Editor MACHINE {\n"
             "    OPTION response_a JSON_VALUE {};\n"
             "    OPTION response_b JSON_VALUE {};\n"
             "    OPTION qsel JSON_VALUE [\n"
             "        {\"action\":\"select\",\"auth\":\"xxx\",\"type\":\"customer\",\n"
             "         \"where\":{\"id\":1},\"respond_to\":\"ed.response_a\"},\n"
             "        {\"action\":\"select\",\"auth\":\"xxx\",\"type\":\"customer\",\n"
             "         \"where\":{\"id\":2},\"respond_to\":\"ed.response_b\"}\n"
             "    ];\n"
             "    COMMAND batch_query {\n"
             "        SEND qsel TO DATABASE_CHANNEL;\n"
             "    }\n"
             "    OPTION qins JSON_VALUE [\n"
             "        {\"action\":\"insert\",\"auth\":\"xxx\",\"type\":\"customer\",\n"
             "         \"data\":{\"id\":3,\"name\":\"Cara\"},\"respond_to\":\"ed.response_a\"},\n"
             "        {\"action\":\"insert\",\"auth\":\"xxx\",\"type\":\"customer\",\n"
             "         \"data\":{\"id\":4,\"name\":\"Dana\"},\"respond_to\":\"ed.response_a\"}\n"
             "    ];\n"
             "    COMMAND batch_insert {\n"
             "        SEND qins TO DATABASE_CHANNEL;\n"
             "    }\n"
             "    OPTION qalt JSON_VALUE {\n"
             "        \"action\":\"insert\",\"auth\":\"xxx\",\"type\":\"customer\",\n"
             "        \"data\":{\"id\":9,\"name\":\"Alt\"}\n"
             "    };\n"
             "    COMMAND alt_insert {\n"
             "        SEND qalt TO ALT_CHANNEL;\n"
             "    }\n"
             "}\n"
             "ed Editor;\n"
             "DATABASE_CHANNEL CHANNEL {\n"
             "    OPTION HOST \"localhost\";\n"
             "    OPTION port %d;\n"
             "    THROTTLE 50;\n"
             "    PUBLISHER;\n"
             "    IGNORES STATE_CHANGES, PROPERTY_CHANGES;\n"
             "}\n"
             "ALT_CHANNEL CHANNEL {\n"
             "    OPTION HOST \"localhost\";\n"
             "    OPTION port %d;\n"
             "    THROTTLE 50;\n"
             "    PUBLISHER;\n"
             "    IGNORES STATE_CHANGES, PROPERTY_CHANGES;\n"
             "}\n",
             ch_default, ch_alt);
    write_text(app + "/main.cw", main_cw);

    char db[128], pbuf[16], nbuf[16];
    snprintf(db, sizeof(db), "%s/store.db", root);
    snprintf(pbuf, sizeof(pbuf), "%d", dport);
    snprintf(nbuf, sizeof(nbuf), "%d", nport);
    char dep[64], nep[64];
    snprintf(dep, sizeof(dep), "tcp://127.0.0.1:%d", dport);
    snprintf(nep, sizeof(nep), "tcp://127.0.0.1:%d", nport);

    std::vector<std::string> logs;
    auto logpath = [&](const char *name) {
        std::string p = std::string(root) + "/" + name;
        logs.push_back(p);
        return p;
    };

    std::vector<pid_t> pids;
    pids.push_back(spawn_logged(logpath("dbsvr.log").c_str(), dbsvr,
                                {"--db", db, "--port", pbuf, "--notify-port", nbuf}));
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
    const char *seed1 =
        "{\"action\":\"insert\",\"auth\":\"xxx\",\"type\":\"customer\","
        "\"data\":{\"id\":1,\"name\":\"Ann\"}}";
    const char *seed2 =
        "{\"action\":\"insert\",\"auth\":\"xxx\",\"type\":\"customer\","
        "\"data\":{\"id\":2,\"name\":\"Bob\"}}";
    if (!req(dbs, seed1, reply, 40) || !req(dbs, seed2, reply, 40)) {
        std::cerr << "dbsvr seed failed: " << reply << "\n";
        kill_all(pids);
        return 2;
    }

    char cpbuf[16], ppubbuf[16], psbuf[16], mpbuf[16];
    snprintf(cpbuf, sizeof(cpbuf), "%d", cmd);
    snprintf(ppubbuf, sizeof(ppubbuf), "%d", pub);
    snprintf(psbuf, sizeof(psbuf), "%d", ps);
    snprintf(mpbuf, sizeof(mpbuf), "%d", mp);
    pids.push_back(spawn_logged(logpath("cw.log").c_str(), cwbin,
                                {"-cp", cpbuf, "-p", ppubbuf, "-ps", psbuf, "-mp", mpbuf, "--name",
                                 "clock_batch", "--nostats", app}));
    usleep(500000);

    zmq::socket_t iod = connect_req(ctx, cmd);
    {
        std::string r;
        if (!cw_get(iod, "ed", "response_a", r)) {
            std::cerr << "cw did not answer GET ed response_a\n";
            kill_all(pids);
            return 3;
        }
    }

    pids.push_back(spawn_logged(logpath("dbd_default.log").c_str(), dbd,
                                {"--host", "127.0.0.1", "--cwout", cpbuf, "--dbsvr", dep, "--notify",
                                 nep}));
    pids.push_back(spawn_logged(logpath("dbd_alt.log").c_str(), dbd,
                                {"--host", "127.0.0.1", "--cwout", cpbuf, "--dbsvr", dep, "--notify",
                                 nep, "--channel", "ALT_CHANNEL"}));
    usleep(900000);

    // --- Batch pass-through: one message, two inserts, both must commit. -----
    std::string batch_insert =
        MessageEncoding::encodeCommand("SEND", Value("batch_insert"), Value("TO"), Value("ed"));
    if (!req(iod, batch_insert, reply, 10)) {
        std::cerr << "SEND batch_insert TO ed failed: " << reply << "\n";
        kill_all(pids);
        return 4;
    }
    if (!dbsvr_has(dbs, "Cara", 50) || !dbsvr_has(dbs, "Dana", 50)) {
        std::cerr << "batch insert did not commit both rows (Cara/Dana)\n";
        kill_all(pids);
        return 5;
    }

    // --- Batch reply fan-out: two selects, each routes to its own respond_to. -
    std::string batch_query =
        MessageEncoding::encodeCommand("SEND", Value("batch_query"), Value("TO"), Value("ed"));
    if (!req(iod, batch_query, reply, 10)) {
        std::cerr << "SEND batch_query TO ed failed: " << reply << "\n";
        kill_all(pids);
        return 6;
    }
    if (!wait_get(iod, "ed", "response_a", "Ann", 50)) {
        std::cerr << "batch select entry 1 did not route Ann to ed.response_a\n";
        kill_all(pids);
        return 7;
    }
    if (!wait_get(iod, "ed", "response_b", "Bob", 50)) {
        std::cerr << "batch select entry 2 did not route Bob to ed.response_b\n";
        kill_all(pids);
        return 8;
    }

    // --- --channel: a request on ALT_CHANNEL reaches the second dbd. ---------
    std::string alt_insert =
        MessageEncoding::encodeCommand("SEND", Value("alt_insert"), Value("TO"), Value("ed"));
    if (!req(iod, alt_insert, reply, 10)) {
        std::cerr << "SEND alt_insert TO ed failed: " << reply << "\n";
        kill_all(pids);
        return 9;
    }
    if (!dbsvr_has(dbs, "Alt", 50)) {
        std::cerr << "dbd --channel ALT_CHANNEL did not deliver the request\n";
        kill_all(pids);
        return 10;
    }

    kill_all(pids);
    std::cout << "ok\n";
    return 0;
}
