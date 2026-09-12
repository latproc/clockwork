// Regression test: a client must re-request CHANNEL quickly once the server
// defines it.
//
// The setup server answers every CHANNEL request with
//   {"error":"No such channel: DATABASE_CHANNEL"}
// until the channel is "defined" (grant_at), then grants it. The measured
// quantity is how long after grant_at dbd's next CHANNEL request arrives.
//
// This used to be ~9s: a channel-error reply moved the FSM to e_error, whose
// timeout is 10s, and the socket recreate that follows is rate-limited to 2s.
// So dbd needed ~10s to find DATABASE_CHANNEL and subscribe after iod defined
// it, which is why startup scripts waited around before checking status.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <zmq.hpp>

static uint64_t now_us() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

static pid_t spawn_dbd(const char *bin, int cw_port) {
    pid_t pid = fork();
    if (pid == 0) {
        char pbuf[16];
        snprintf(pbuf, sizeof(pbuf), "%d", cw_port);
        // No dbsvr/notify peers: this test only exercises CHANNEL setup.
        execl(bin, bin, "--host", "127.0.0.1", "--cwout", pbuf, "--dbsvr", "tcp://127.0.0.1:1",
              "--notify", "tcp://127.0.0.1:1", static_cast<char *>(0));
        _exit(127);
    }
    return pid;
}

static void stop_dbd(pid_t pid) {
    kill(pid, SIGTERM);
    usleep(200000);
    kill(pid, SIGKILL);
    waitpid(pid, 0, 0);
}

int main() {
    const char *dbd = getenv("DBD");
    if (!dbd || !*dbd) {
        std::cerr << "DBD not set\n";
        return 77;
    }

    const int base = 22000 + static_cast<int>(getpid() % 500);
    const int cw_port = base;
    const int sub_port = base + 1;
    char ep[64], sub_ep[64];
    snprintf(ep, sizeof(ep), "tcp://127.0.0.1:%d", cw_port);
    snprintf(sub_ep, sizeof(sub_ep), "tcp://127.0.0.1:%d", sub_port);

    zmq::context_t ctx;
    zmq::socket_t rep(ctx, ZMQ_REP);
    int linger = 0;
    rep.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    rep.bind(ep);
    // The granted subscriber port has a listener so dbd can reach e_done.
    zmq::socket_t pub(ctx, ZMQ_PUB);
    pub.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    pub.bind(sub_ep);

    pid_t pid = spawn_dbd(dbd, cw_port);

    uint64_t first_req = 0;
    uint64_t grant_at = 0;
    uint64_t arrival = 0;
    bool saw_channel_error = false;
    const uint64_t window_end = now_us() + 20000000ULL; // 20s ceiling

    while (now_us() < window_end) {
        zmq::pollitem_t items[] = {{rep, 0, ZMQ_POLLIN, 0}};
        if (zmq::poll(items, 1, 50) <= 0) {
            continue;
        }
        zmq::message_t req;
        if (!rep.recv(&req, 0)) {
            continue;
        }
        const uint64_t now = now_us();
        if (first_req == 0) {
            first_req = now;
            grant_at = now + 1500000ULL; // define the channel 1.5s after first ask
        }
        if (now >= grant_at) {
            arrival = now;
            char grant[160];
            snprintf(grant, sizeof(grant),
                     "{\"port\":%d,\"name\":\"DATABASE_CHANNEL\",\"authority\":1}", sub_port);
            zmq::message_t reply(strlen(grant));
            memcpy(reply.data(), grant, strlen(grant));
            rep.send(reply, 0);
            break;
        }
        saw_channel_error = true;
        const char *err = "{\"error\":\"No such channel: DATABASE_CHANNEL\"}";
        zmq::message_t reply(strlen(err));
        memcpy(reply.data(), err, strlen(err));
        rep.send(reply, 0);
    }

    stop_dbd(pid);

    if (!saw_channel_error) {
        std::cerr << "dbd never sent a CHANNEL request before the grant\n";
        return 1;
    }
    if (arrival == 0) {
        std::cerr << "dbd did not re-request CHANNEL within 20s of the grant\n";
        return 1;
    }
    const long delay_ms = static_cast<long>((arrival - grant_at) / 1000ULL);
    if (delay_ms > 3000) {
        std::cerr << "dbd took " << delay_ms
                  << "ms to re-request CHANNEL after it was defined (expected < 3000ms)\n";
        return 1;
    }
    std::cout << "dbd re-requested CHANNEL " << delay_ms << "ms after it was defined\n";
    return 0;
}
