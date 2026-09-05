#include "DeadlineReq.h"
#include "MessagingInterface.h"
#include "value.h"
#include <iostream>
#include <string>
#include <string.h>
#include <thread>
#include <unistd.h>
#include <zmq.hpp>

int main() {
    zmq::context_t ctx;
    MessagingInterface::setContext(&ctx);
    const char *ep = "tcp://127.0.0.1:15554";
    zmq::socket_t *rep = new zmq::socket_t(ctx, ZMQ_REP);
    int linger = 0;
    rep->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    rep->bind(ep);

    std::thread server([&]() {
        for (int i = 0; i < 2; ++i) {
            zmq::message_t m;
            if (!rep->recv(&m, 0)) {
                return;
            }
            const char *ok = "{\"status\":0}";
            zmq::message_t out(strlen(ok));
            memcpy(out.data(), ok, strlen(ok));
            rep->send(out, 0);
            if (i == 0) {
                delete rep;
                usleep(50000);
                rep = new zmq::socket_t(ctx, ZMQ_REP);
                int linger0 = 0;
                rep->setsockopt(ZMQ_LINGER, &linger0, sizeof(linger0));
                try {
                    rep->bind(ep);
                }
                catch (...) {
                    return;
                }
            }
        }
        delete rep;
        rep = 0;
    });

    usleep(100000);
    DeadlineReq client(ctx, ep);
    std::string reply;
    if (!client.request("{\"action\":\"ping\"}", reply, 2000)) {
        std::cerr << "first request failed\n";
        server.join();
        return 1;
    }
    client.reconnect();
    usleep(150000);
    if (!client.request("{\"action\":\"ping\"}", reply, 2000)) {
        std::cerr << "request after peer restart failed\n";
        server.join();
        return 2;
    }
    server.join();
    std::cout << "ok\n";
    return 0;
}
