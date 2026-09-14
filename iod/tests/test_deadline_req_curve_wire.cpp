// Wire-level CurveZMQ tests for DeadlineReq against a real CURVE server.
//
// The configuration test (test_deadline_req_curve.cpp) proves the options are set
// per connection. This proves they actually take effect on the wire:
//
//   1. a client configured with the server's public key completes a handshake and
//      exchanges a request/reply;
//   2. a client holding the WRONG server key is rejected, and - the point of the
//      negative test - is NOT silently downgraded to plaintext. PLAN.md section
//      7.8 requires exactly this: "a client with the wrong key, or with CURVE
//      disabled, must be rejected, never silently downgraded";
//   3. a plain client is refused by a CURVE server.
//
// Self-contained. See iod/AGENTS.md.

#include "DeadlineReq.h"
#include "MessagingInterface.h"
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <zmq.hpp>

static int failures = 0;

static void check(bool ok, const char *what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
    else {
        std::cout << "ok:   " << what << "\n";
    }
}

// Serves exactly one CURVE request, then stops. Reports whether a request ever
// arrived, which is what distinguishes "connection refused" from "connected".
struct CurveServer {
    zmq::socket_t sock;
    std::thread thread;
    std::atomic<bool> gotRequest{false};

    CurveServer(zmq::context_t &ctx, const char *endpoint, const char *pub, const char *sec)
        : sock(ctx, ZMQ_REP) {
        sock.setsockopt(ZMQ_LINGER, 0);
        sock.setsockopt(ZMQ_CURVE_SERVER, 1);
        sock.setsockopt(ZMQ_CURVE_PUBLICKEY, pub, 40);
        sock.setsockopt(ZMQ_CURVE_SECRETKEY, sec, 40);
        sock.bind(endpoint);

        thread = std::thread([this]() {
            zmq::pollitem_t items[] = {{(void *)sock, 0, ZMQ_POLLIN, 0}};
            // Long enough for a legitimate handshake, short enough to keep the
            // test quick when a handshake is expected to fail.
            if (zmq::poll(items, 1, 1500) > 0) {
                zmq::message_t msg;
                if (sock.recv(msg, zmq::recv_flags::none)) {
                    gotRequest = true;
                    const char *reply = "{\"status\":0}";
                    zmq::message_t out(strlen(reply));
                    memcpy(out.data(), reply, strlen(reply));
                    sock.send(out, zmq::send_flags::none);
                }
            }
        });
    }

    void join() {
        if (thread.joinable()) {
            thread.join();
        }
    }
};

int main() {
    zmq::context_t ctx;
    MessagingInterface::setContext(&ctx);

    char server_pub[41] = {0};
    char server_sec[41] = {0};
    char client_pub[41] = {0};
    char client_sec[41] = {0};
    char other_pub[41] = {0};
    char other_sec[41] = {0};

    if (zmq_curve_keypair(server_pub, server_sec) != 0 ||
        zmq_curve_keypair(client_pub, client_sec) != 0 ||
        zmq_curve_keypair(other_pub, other_sec) != 0) {
        std::cerr << "could not generate CURVE keypairs\n";
        return 1;
    }

    // --- 1. correct key: the handshake completes -------------------------
    {
        CurveServer server(ctx, "tcp://127.0.0.1:15871", server_pub, server_sec);

        CurveOptions opts;
        opts.serverKey = std::string(server_pub, 40);
        opts.clientPublicKey = std::string(client_pub, 40);
        opts.clientSecretKey = std::string(client_sec, 40);

        DeadlineReq req(ctx, "tcp://127.0.0.1:15871", opts);
        std::string reply;
        bool ok = req.request("{\"action\":\"find\"}", reply, 2000);

        check(ok, "CURVE client with the correct server key exchanges a request");
        check(server.gotRequest.load(), "server received the request over CURVE");
        server.join();
    }

    // --- 2. WRONG server key: refused, never downgraded -------------------
    {
        CurveServer server(ctx, "tcp://127.0.0.1:15872", server_pub, server_sec);

        CurveOptions wrong;
        // A valid key, but not this server's: the handshake must fail.
        wrong.serverKey = std::string(other_pub, 40);
        wrong.clientPublicKey = std::string(client_pub, 40);
        wrong.clientSecretKey = std::string(client_sec, 40);

        DeadlineReq req(ctx, "tcp://127.0.0.1:15872", wrong);
        std::string reply;
        bool ok = req.request("{\"action\":\"find\"}", reply, 2000);

        check(!ok, "CURVE client with the WRONG server key is refused");
        check(!server.gotRequest.load(),
              "wrong-key client never reached the server (no plaintext fallback)");
        server.join();
    }

    // --- 3. plain client against a CURVE server --------------------------
    {
        CurveServer server(ctx, "tcp://127.0.0.1:15873", server_pub, server_sec);

        DeadlineReq req(ctx, "tcp://127.0.0.1:15873");   // no CURVE options
        std::string reply;
        bool ok = req.request("{\"action\":\"find\"}", reply, 2000);

        check(!ok, "plain client is refused by a CURVE server");
        check(!server.gotRequest.load(), "plain client never reached the server");
        server.join();
    }

    // --- 4. CURVE client against a PLAIN server --------------------------
    // This is the dbd-against-a-plain-dbsvr case: someone enables CURVE on the
    // client but the peer is not a CURVE server. It must fail, not quietly talk
    // in the clear.
    {
        zmq::socket_t plain_server(ctx, ZMQ_REP);
        plain_server.setsockopt(ZMQ_LINGER, 0);
        plain_server.bind("tcp://127.0.0.1:15874");

        std::atomic<bool> gotRequest{false};
        std::thread server([&]() {
            zmq::pollitem_t items[] = {{(void *)plain_server, 0, ZMQ_POLLIN, 0}};
            if (zmq::poll(items, 1, 1500) > 0) {
                zmq::message_t msg;
                if (plain_server.recv(msg, zmq::recv_flags::none)) {
                    gotRequest = true;
                }
            }
        });

        CurveOptions opts;
        opts.serverKey = std::string(server_pub, 40);
        opts.clientPublicKey = std::string(client_pub, 40);
        opts.clientSecretKey = std::string(client_sec, 40);

        DeadlineReq req(ctx, "tcp://127.0.0.1:15874", opts);
        std::string reply;
        bool ok = req.request("{\"action\":\"find\"}", reply, 2000);

        check(!ok, "CURVE client is refused by a plain server");
        check(!gotRequest.load(),
              "CURVE client did not fall back to plaintext against a plain server");
        server.join();
        plain_server.close();
    }

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all CurveZMQ wire checks passed\n";
    return 0;
}
