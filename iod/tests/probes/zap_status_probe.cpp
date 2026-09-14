// Empirically determines what a ZAP handler can make a CURVE client do.
//
// The question: can a server admit an unknown client's key, tell it to wait
// (rather than rejecting it outright), and let it in once approved?
//
// ZAP runs INSIDE the handshake and must answer immediately from a fixed set of
// statuses, so there is no "hold the connection open" option. The candidate is
// status 300 (temporary error). This probe answers what actually happens on the
// wire for each status, rather than assuming.
//
// Method: a server with ZAP + CURVE, a ZAP handler that answers with a
// configurable status, and a REQ client. The handler counts how many times it is
// consulted, which reveals whether the client keeps coming back.
//
// Build: g++ -std=c++17 -o /tmp/zap_probe /tmp/zap_probe.cpp -L/usr/local/lib -lzmq

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <zmq.h>

static int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ZAP handler: reads requests on the inproc ZAP socket and answers with the
// configured status. Must be a REP socket and must reply to every request.
static void zap_loop(void *ctx, std::atomic<int> *status, std::atomic<int> *count,
                     std::atomic<bool> *stop) {
    void *zap = zmq_socket(ctx, ZMQ_REP);
    if (zmq_bind(zap, "inproc://zeromq.zap.01") != 0) {
        std::cerr << "ZAP bind failed: " << zmq_strerror(zmq_errno()) << "\n";
        zmq_close(zap);
        return;
    }

    while (!stop->load()) {
        zmq_pollitem_t items[] = {{zap, 0, ZMQ_POLLIN, 0}};
        if (zmq_poll(items, 1, 200) <= 0) {
            continue;
        }

        // version, request-id, domain, address, identity, mechanism, credentials
        zmq_msg_t parts[7];
        std::string fields[7];
        for (int i = 0; i < 7; i++) {
            zmq_msg_init(&parts[i]);
            if (zmq_msg_recv(&parts[i], zap, 0) < 0) {
                for (int j = 0; j <= i; j++) zmq_msg_close(&parts[j]);
                goto done;
            }
            fields[i].assign((const char *)zmq_msg_data(&parts[i]), zmq_msg_size(&parts[i]));
            zmq_msg_close(&parts[i]);
        }

        (*count)++;
        {
            const char *mech = fields[5].c_str();
            std::string key = fields[6];
            std::cout << "  [zap] consult #" << count->load() << " mechanism=" << mech
                      << " credential-bytes=" << key.size();
            if (key.size() >= 32) {
                // The CURVE credential is the 32-byte client public key.
                std::cout << " client-key-prefix=";
                for (int i = 0; i < 4; i++) {
                    char b[8];
                    snprintf(b, sizeof(b), "%02x", (unsigned char)key[i]);
                    std::cout << b;
                }
            }
            std::cout << "\n" << std::flush;
        }

        // Reply: version, request-id, status-code, status-text, user-id, metadata
        char code[8];
        snprintf(code, sizeof(code), "%d", status->load());
        const char *text = status->load() == 200   ? "OK"
                           : status->load() == 300 ? "Temporary error"
                                                   : "Denied";
        zmq_send(zap, "1.0", 3, ZMQ_SNDMORE);
        zmq_send(zap, fields[1].c_str(), fields[1].size(), ZMQ_SNDMORE);
        zmq_send(zap, code, strlen(code), ZMQ_SNDMORE);
        zmq_send(zap, text, strlen(text), ZMQ_SNDMORE);
        zmq_send(zap, "", 0, ZMQ_SNDMORE);
        zmq_send(zap, "", 0, 0);
    }
done:
    zmq_close(zap);
}

// Runs one scenario: server with the given ZAP status, one client attempt.
static void scenario(const char *label, int zapStatus, int waitMs) {
    std::cout << "\n=== " << label << " (ZAP status " << zapStatus << ") ===\n";

    void *ctx = zmq_ctx_new();
    std::atomic<int> status{zapStatus};
    std::atomic<int> count{0};
    std::atomic<bool> stop{false};

    std::thread zap(zap_loop, ctx, &status, &count, &stop);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    char server_pub[41] = {0}, server_sec[41] = {0};
    char client_pub[41] = {0}, client_sec[41] = {0};
    zmq_curve_keypair(server_pub, server_sec);
    zmq_curve_keypair(client_pub, client_sec);

    void *server = zmq_socket(ctx, ZMQ_REP);
    int one = 1, linger = 0;
    zmq_setsockopt(server, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(server, ZMQ_CURVE_SERVER, &one, sizeof(one));
    zmq_setsockopt(server, ZMQ_CURVE_PUBLICKEY, server_pub, 40);
    zmq_setsockopt(server, ZMQ_CURVE_SECRETKEY, server_sec, 40);
    // Without a ZAP domain, the handler is not consulted at all.
    zmq_setsockopt(server, ZMQ_ZAP_DOMAIN, "test", 4);

    const char *ep = "tcp://127.0.0.1:15961";
    zmq_bind(server, ep);

    // Actually serve the request, otherwise a successful handshake still looks
    // like a timeout and the status codes cannot be told apart.
    std::atomic<bool> served{false};
    std::thread serverThread([&]() {
        zmq_pollitem_t srvItems[] = {{server, 0, ZMQ_POLLIN, 0}};
        if (zmq_poll(srvItems, 1, waitMs + 1000) > 0) {
            char buf[64];
            int n = zmq_recv(server, buf, sizeof(buf), 0);
            if (n >= 0) {
                served.store(true);
                zmq_send(server, "world", 5, 0);
            }
        }
    });

    void *client = zmq_socket(ctx, ZMQ_REQ);
    zmq_setsockopt(client, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(client, ZMQ_CURVE_SERVERKEY, server_pub, 40);
    zmq_setsockopt(client, ZMQ_CURVE_PUBLICKEY, client_pub, 40);
    zmq_setsockopt(client, ZMQ_CURVE_SECRETKEY, client_sec, 40);
    zmq_connect(client, ep);

    int64_t t0 = now_ms();
    zmq_send(client, "hello", 5, 0);

    zmq_pollitem_t items[] = {{client, 0, ZMQ_POLLIN, 0}};
    int rc = zmq_poll(items, 1, waitMs);
    int64_t elapsed = now_ms() - t0;

    if (rc > 0) {
        char buf[64] = {0};
        int n = zmq_recv(client, buf, sizeof(buf) - 1, 0);
        std::cout << "  result: REPLIED after " << elapsed << " ms (" << n << " bytes)\n";
    }
    else {
        std::cout << "  result: NO REPLY within " << waitMs << " ms (elapsed " << elapsed
                  << " ms)\n";
    }
    std::cout << "  ZAP consulted " << count.load() << " time(s)\n";

    // The crucial measurement: does the client keep retrying while it waits?
    // Measured over a window long enough for libzmq's reconnect backoff.
    int before = count.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    int after = count.load();
    std::cout << "  ZAP consultations during a further 5000 ms idle: " << (after - before)
              << "  <-- non-zero means the client retries the handshake\n";

    serverThread.join();
    zmq_close(client);
    zmq_close(server);
    stop.store(true);
    zap.join();
    zmq_ctx_term(ctx);
}

int main() {
    int major = 0, minor = 0, patch = 0;
    zmq_version(&major, &minor, &patch);
    std::cout << "libzmq " << major << "." << minor << "." << patch << "\n";
    scenario("approved key", 200, 2000);
    scenario("explicitly denied key", 400, 2000);
    scenario("temporary error (pending approval)", 300, 3000);
    return 0;
}
