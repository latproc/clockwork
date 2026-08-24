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
        if (body.find("RECORD_APPLY") != std::string::npos &&
            body.find("Ann") != std::string::npos) {
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

static pid_t spawn_dbd(const char *bin, int cw_port, const char *notify) {
    pid_t pid = fork();
    if (pid == 0) {
        char pbuf[16];
        snprintf(pbuf, sizeof(pbuf), "%d", cw_port);
        execl(bin, bin, "--host", "127.0.0.1", "--cwout", pbuf, "--dbsvr",
              "tcp://127.0.0.1:1", "--notify", notify, static_cast<char *>(0));
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

    pid_t dbd_a = spawn_dbd(dbd, port_a, nep);
    pid_t dbd_b = spawn_dbd(dbd, port_b, nep);
    usleep(800000);

    const char *payload =
        "{\"action\":\"insert\",\"type\":\"customer\",\"keys\":{\"id\":1},"
        "\"row\":{\"id\":1,\"name\":\"Ann\"}}";
    zmq::message_t m(std::strlen(payload));
    std::memcpy(m.data(), payload, std::strlen(payload));
    pub.send(m, 0);

    bool ok = false;
    for (int i = 0; i < 50; ++i) {
        if (file_is_ann(path_a) && file_is_ann(path_b)) {
            ok = true;
            break;
        }
        usleep(100000);
    }

    kill(dbd_a, SIGTERM);
    kill(dbd_b, SIGTERM);
    kill(iod_a, SIGTERM);
    kill(iod_b, SIGTERM);
    usleep(100000);
    kill(dbd_a, SIGKILL);
    kill(dbd_b, SIGKILL);
    kill(iod_a, SIGKILL);
    kill(iod_b, SIGKILL);
    waitpid(dbd_a, 0, 0);
    waitpid(dbd_b, 0, 0);
    waitpid(iod_a, 0, 0);
    waitpid(iod_b, 0, 0);
    unlink(path_a);
    unlink(path_b);
    if (!ok) {
        std::cerr << "two dbd did not both RECORD_APPLY Ann\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
