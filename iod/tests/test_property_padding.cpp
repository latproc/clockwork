// Regression test: a scalar PROPERTY value that looks numeric but carries
// leading whitespace must stay a string.
//
// IODCommandProperty decides whether a STRING/SYMBOL value is an integer with
// strtol() + (*p == 0). strtol skips leading whitespace, so a padded fixed-width
// key such as "  267968" used to be judged an integer, coerced to 267968 and
// stripped of its padding on the way through the property. This drives the real
// cw command interface (PROPERTY then GET) to pin the behaviour.
//
// Needs CW in the environment (see CMakeLists.txt).
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

// Set `pad.s` to the given string value and read it back; returns the raw GET
// reply so the caller can check exactly what the property holds.
static bool set_and_get(zmq::socket_t &iod, const std::string &value, std::string &reply) {
    std::string setcmd = MessageEncoding::encodeCommand(
        "PROPERTY", Value("ed"), Value("s"), Value(value, Value::t_string));
    std::string ignored;
    if (!req(iod, setcmd, ignored, 10)) {
        return false;
    }
    std::string getcmd = MessageEncoding::encodeCommand("GET", Value("ed"), Value("s"));
    return req(iod, getcmd, reply, 10);
}

static bool wait_ready(zmq::socket_t &iod, int tries) {
    std::string getcmd = MessageEncoding::encodeCommand("GET", Value("ed"), Value("s"));
    std::string last;
    for (int i = 0; i < tries; ++i) {
        std::string reply;
        if (req(iod, getcmd, reply, 2)) {
            last = reply;
            if (reply.find("unset") != std::string::npos &&
                reply.find("Unknown") == std::string::npos) {
                return true;
            }
        }
        usleep(100000);
    }
    std::cerr << "last GET reply: [" << last << "]\n";
    return false;
}

int main() {
    const char *cwbin = getenv("CW");
    if (!cwbin || !*cwbin) {
        std::cerr << "CW must be set\n";
        return 77;
    }

    const int base = 25000 + static_cast<int>(getpid() % 400);
    const int cmd = base;
    const int pub = base + 1;
    const int ps = base + 2;
    const int mp = base + 3;

    char root[128];
    snprintf(root, sizeof(root), "/tmp/cw_prop_pad_%d", static_cast<int>(getpid()));
    std::string app = std::string(root) + "/app";
    mkdir(root, 0755);
    mkdir(app.c_str(), 0755);
    if (!write_text(app + "/main.cw",
                    "Editor MACHINE {\n"
                    "    OPTION s \"unset\";\n"
                    "}\n"
                    "ed Editor;\n")) {
        std::cerr << "could not write test program\n";
        return 1;
    }

    char cpbuf[16], ppubbuf[16], psbuf[16], mpbuf[16];
    snprintf(cpbuf, sizeof(cpbuf), "%d", cmd);
    snprintf(ppubbuf, sizeof(ppubbuf), "%d", pub);
    snprintf(psbuf, sizeof(psbuf), "%d", ps);
    snprintf(mpbuf, sizeof(mpbuf), "%d", mp);

    std::string log = std::string(root) + "/cw.log";
    std::vector<pid_t> pids;
    pids.push_back(spawn_logged(log.c_str(), cwbin,
                                {"-cp", cpbuf, "-p", ppubbuf, "-ps", psbuf, "-mp", mpbuf, "--name",
                                 "clock_pad", "--nostats", app}));
    usleep(500000);

    zmq::context_t ctx;
    zmq::socket_t iod = connect_req(ctx, cmd);
    if (!wait_ready(iod, 50)) {
        std::cerr << "cw did not answer GET pad s\n";
        kill_all(pids);
        return 1;
    }

    // Padded fixed-width key: leading spaces must survive.
    std::string reply;
    if (!set_and_get(iod, "  267968", reply) || reply.find("  267968") == std::string::npos) {
        std::cerr << "padded string lost its leading spaces: " << reply << "\n";
        kill_all(pids);
        return 2;
    }

    // Leading and trailing padding (this case already worked; keep it guarded).
    if (!set_and_get(iod, " 267968 ", reply) || reply.find(" 267968 ") == std::string::npos) {
        std::cerr << "leading+trailing padding not preserved: " << reply << "\n";
        kill_all(pids);
        return 3;
    }

    // A genuinely numeric string is still an integer, with no stray padding.
    if (!set_and_get(iod, "267968", reply) || reply.find("267968") == std::string::npos ||
        reply.find("  267968") != std::string::npos) {
        std::cerr << "plain numeric value not stored as an integer: " << reply << "\n";
        kill_all(pids);
        return 4;
    }

    // A negative number still parses.
    if (!set_and_get(iod, "-42", reply) || reply.find("-42") == std::string::npos) {
        std::cerr << "negative integer not handled: " << reply << "\n";
        kill_all(pids);
        return 5;
    }

    // The empty string still clears to "" rather than becoming the integer 0.
    if (!set_and_get(iod, "", reply) || reply.find("267968") != std::string::npos ||
        reply.find("-42") != std::string::npos) {
        std::cerr << "empty string did not clear the property: " << reply << "\n";
        kill_all(pids);
        return 6;
    }

    kill_all(pids);
    std::cout << "ok\n";
    return 0;
}
