#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <zmq.hpp>

static int run_iod(const char *endpoint, const char *outpath) {
    zmq::context_t ctx;
    zmq::socket_t rep(ctx, ZMQ_REP);
    int linger = 0;
    rep.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    rep.bind(endpoint);
    for (;;) {
        zmq::pollitem_t items[] = {{rep, 0, ZMQ_POLLIN, 0}};
        if (zmq::poll(items, 1, 100) <= 0) {
            continue;
        }
        zmq::message_t m;
        if (!rep.recv(&m, 0)) {
            continue;
        }
        std::string body(static_cast<char *>(m.data()), m.size());
        if (body.find("Ann") != std::string::npos &&
            (body.find("RECORD_APPLY") != std::string::npos ||
             body.find("APPLY") != std::string::npos)) {
            std::ofstream out(outpath);
            out << "Ann";
        }
        const char *ok = "OK 1";
        zmq::message_t reply(4);
        memcpy(reply.data(), ok, 4);
        rep.send(reply, 0);
    }
}

static pid_t spawn_iod(const char *self, const char *ep, const char *path) {
    pid_t pid = fork();
    if (pid == 0) {
        execl(self, self, "--iod", ep, path, static_cast<char *>(0));
        _exit(127);
    }
    return pid;
}

static pid_t spawn_dbd(const char *bin, int cw_port, const char *notify, const char *dbsvr) {
    pid_t pid = fork();
    if (pid == 0) {
        char pbuf[16];
        snprintf(pbuf, sizeof(pbuf), "%d", cw_port);
        execl(bin, bin, "--host", "127.0.0.1", "--cwout", pbuf, "--dbsvr", dbsvr, "--notify",
              notify, static_cast<char *>(0));
        _exit(127);
    }
    return pid;
}

static bool file_is_ann(const char *path) {
    std::ifstream in(path);
    std::string s;
    in >> s;
    return s == "Ann";
}

static bool wait_ann(const char *path_a, const char *path_b) {
    for (int i = 0; i < 50; ++i) {
        if (file_is_ann(path_a) && file_is_ann(path_b)) {
            return true;
        }
        usleep(100000);
    }
    return false;
}

static void kill_wait(pid_t a, pid_t b, pid_t c = 0, pid_t d = 0) {
    pid_t pids[] = {a, b, c, d};
    for (int i = 0; i < 4; ++i) {
        if (pids[i] > 0) {
            kill(pids[i], SIGTERM);
        }
    }
    usleep(100000);
    for (int i = 0; i < 4; ++i) {
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
            std::memcpy(m.data(), json.data(), json.size());
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

int main(int argc, char **argv) {
    if (argc == 4 && std::strcmp(argv[1], "--iod") == 0) {
        return run_iod(argv[2], argv[3]);
    }
    const char *dbd = getenv("DBD");
    if (!dbd || !*dbd) {
        std::cerr << "DBD not set\n";
        return 77;
    }

    const int base = 20700 + static_cast<int>(getpid() % 500);
    const int port_a = base;
    const int port_b = base + 1;
    const int nport = base + 2;
    char ep_a[64], ep_b[64], nep[64];
    snprintf(ep_a, sizeof(ep_a), "tcp://127.0.0.1:%d", port_a);
    snprintf(ep_b, sizeof(ep_b), "tcp://127.0.0.1:%d", port_b);
    snprintf(nep, sizeof(nep), "tcp://127.0.0.1:%d", nport);
    char path_a[128], path_b[128];
    snprintf(path_a, sizeof(path_a), "/tmp/cw_two_dbd_%d_a", static_cast<int>(getpid()));
    snprintf(path_b, sizeof(path_b), "/tmp/cw_two_dbd_%d_b", static_cast<int>(getpid()));
    unlink(path_a);
    unlink(path_b);

    pid_t iod_a = spawn_iod(argv[0], ep_a, path_a);
    pid_t iod_b = spawn_iod(argv[0], ep_b, path_b);
    usleep(200000);

    zmq::context_t ctx;
    zmq::socket_t pub(ctx, ZMQ_PUB);
    int linger = 0;
    pub.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    pub.bind(nep);

    pid_t dbd_a = spawn_dbd(dbd, port_a, nep, "tcp://127.0.0.1:1");
    pid_t dbd_b = spawn_dbd(dbd, port_b, nep, "tcp://127.0.0.1:1");
    usleep(800000);

    const char *payload =
        "{\"action\":\"insert\",\"type\":\"customer\",\"keys\":{\"id\":1},"
        "\"row\":{\"id\":1,\"name\":\"Ann\"}}";
    zmq::message_t m(std::strlen(payload));
    std::memcpy(m.data(), payload, std::strlen(payload));
    pub.send(m, 0);

    bool ok = wait_ann(path_a, path_b);
    kill_wait(dbd_a, dbd_b, iod_a, iod_b);
    unlink(path_a);
    unlink(path_b);
    if (!ok) {
        std::cerr << "two dbd did not both RECORD_APPLY Ann\n";
        return 1;
    }

    const char *dbsvr = getenv("DBSVR");
    if (dbsvr && *dbsvr) {
        const int lbase = base + 10;
        const int lport_a = lbase;
        const int lport_b = lbase + 1;
        const int lnport = lbase + 2;
        const int dport = lbase + 3;
        char lep_a[64], lep_b[64], lnep[64], dep[64], db[128], pbuf[16], nbuf[16];
        snprintf(lep_a, sizeof(lep_a), "tcp://127.0.0.1:%d", lport_a);
        snprintf(lep_b, sizeof(lep_b), "tcp://127.0.0.1:%d", lport_b);
        snprintf(lnep, sizeof(lnep), "tcp://127.0.0.1:%d", lnport);
        snprintf(dep, sizeof(dep), "tcp://127.0.0.1:%d", dport);
        snprintf(db, sizeof(db), "/tmp/cw_two_dbd_%d.db", static_cast<int>(getpid()));
        snprintf(pbuf, sizeof(pbuf), "%d", dport);
        snprintf(nbuf, sizeof(nbuf), "%d", lnport);
        char lpath_a[128], lpath_b[128];
        snprintf(lpath_a, sizeof(lpath_a), "/tmp/cw_two_dbd_%d_la", static_cast<int>(getpid()));
        snprintf(lpath_b, sizeof(lpath_b), "/tmp/cw_two_dbd_%d_lb", static_cast<int>(getpid()));
        unlink(lpath_a);
        unlink(lpath_b);
        unlink(db);
        unlink((std::string(db) + "-wal").c_str());
        unlink((std::string(db) + "-shm").c_str());

        pid_t svr = fork();
        if (svr == 0) {
            execl(dbsvr, dbsvr, "--db", db, "--port", pbuf, "--notify-port", nbuf,
                  static_cast<char *>(0));
            _exit(127);
        }
        pid_t liod_a = spawn_iod(argv[0], lep_a, lpath_a);
        pid_t liod_b = spawn_iod(argv[0], lep_b, lpath_b);
        usleep(200000);
        pid_t ldbd_a = spawn_dbd(dbd, lport_a, lnep, dep);
        pid_t ldbd_b = spawn_dbd(dbd, lport_b, lnep, dep);
        usleep(800000);

        zmq::socket_t client(ctx, ZMQ_REQ);
        int linger0 = 0;
        client.setsockopt(ZMQ_LINGER, &linger0, sizeof(linger0));
        client.connect(dep);
        std::string reply;
        const char *create =
            "{\"action\":\"create\",\"auth\":\"xxx\",\"type\":\"customer\","
            "\"schema\":{\"id\":\"integer primary key\",\"name\":\"text\"}}";
        const char *ins =
            "{\"action\":\"insert\",\"auth\":\"xxx\",\"type\":\"customer\","
            "\"data\":{\"id\":1,\"name\":\"Ann\"}}";
        if (!req(client, create, reply, 40) || !req(client, ins, reply, 10) ||
            reply.find("Ann") == std::string::npos) {
            std::cerr << "live dbsvr insert failed: " << reply << "\n";
            kill_wait(ldbd_a, ldbd_b, liod_a, liod_b);
            kill_wait(svr, 0);
            return 6;
        }
        bool live_ok = wait_ann(lpath_a, lpath_b);
        kill_wait(ldbd_a, ldbd_b, liod_a, liod_b);
        kill_wait(svr, 0);
        unlink(lpath_a);
        unlink(lpath_b);
        unlink(db);
        unlink((std::string(db) + "-wal").c_str());
        unlink((std::string(db) + "-shm").c_str());
        if (!live_ok) {
            std::cerr << "live dbsvr two dbd did not both RECORD_APPLY Ann\n";
            return 7;
        }
    }
    std::cout << "ok\n";
    return 0;
}
