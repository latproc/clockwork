// Does a libzmq CURVE server see who connected, via ZAP's User-Id?
//
// This decides whether an allow-list is implementable at all. The RFC says the
// server authenticates the client's permanent public key on INITIATE, but the
// question here is what reaches the APPLICATION: RFC 27/ZAP lets a handler return
// a User-Id, which libzmq attaches to the connection.
//
// Method: a ZAP handler that returns a marked User-Id, a CURVE ROUTER server, and
// a CURVE client. If the server's received message carries the User-Id frame, the
// application can identify the peer; if not, it cannot.
//
// Build: g++ -std=c++17 -o /tmp/zap_identity_probe /tmp/zap_identity_probe.cpp -L/usr/local/lib -lzmq

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <zmq.h>

static std::string g_client_key_hex;

static void zap_loop(void *ctx, std::atomic<bool> *stop) {
    void *zap = zmq_socket(ctx, ZMQ_REP);
    zmq_bind(zap, "inproc://zeromq.zap.01");

    while (!stop->load()) {
        zmq_pollitem_t items[] = {{zap, 0, ZMQ_POLLIN, 0}};
        if (zmq_poll(items, 1, 200) <= 0) continue;

        std::string fields[7];
        for (int i = 0; i < 7; i++) {
            zmq_msg_t m;
            zmq_msg_init(&m);
            zmq_msg_recv(&m, zap, 0);
            fields[i].assign((const char *)zmq_msg_data(&m), zmq_msg_size(&m));
            zmq_msg_close(&m);
        }

        // The CURVE credential IS the client's permanent public key (32 bytes).
        const std::string &key = fields[6];
        char hex[80] = {0};
        for (size_t i = 0; i < key.size() && i < 8; i++)
            snprintf(hex + i * 2, 3, "%02x", (unsigned char)key[i]);
        std::cout << "  [zap] mechanism=" << fields[5] << " client-key=" << hex
                  << " (" << key.size() << " bytes)\n";

        // Return a User-Id derived from the key. libzmq attaches this to the
        // connection and should surface it to the application.
        std::string userId = "user-" + std::string(hex);
        const char *code = "200";
        zmq_send(zap, "1.0", 3, ZMQ_SNDMORE);
        zmq_send(zap, fields[1].c_str(), fields[1].size(), ZMQ_SNDMORE);
        zmq_send(zap, code, 3, ZMQ_SNDMORE);
        zmq_send(zap, "OK", 2, ZMQ_SNDMORE);
        zmq_send(zap, userId.c_str(), userId.size(), ZMQ_SNDMORE);
        zmq_send(zap, "", 0, 0);
    }
    zmq_close(zap);
}

int main() {
    void *ctx = zmq_ctx_new();
    std::atomic<bool> stop{false};
    std::thread zap(zap_loop, ctx, &stop);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    char server_pub[41] = {0}, server_sec[41] = {0};
    char client_pub[41] = {0}, client_sec[41] = {0};
    zmq_curve_keypair(server_pub, server_sec);
    zmq_curve_keypair(client_pub, client_sec);

    // ROUTER so identity frames are explicit.
    void *server = zmq_socket(ctx, ZMQ_ROUTER);
    int one = 1, linger = 0;
    zmq_setsockopt(server, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(server, ZMQ_CURVE_SERVER, &one, sizeof(one));
    zmq_setsockopt(server, ZMQ_CURVE_PUBLICKEY, server_pub, 40);
    zmq_setsockopt(server, ZMQ_CURVE_SECRETKEY, server_sec, 40);
    zmq_setsockopt(server, ZMQ_ZAP_DOMAIN, "vfpbridge", 9);
    zmq_bind(server, "tcp://127.0.0.1:15992");

    void *client = zmq_socket(ctx, ZMQ_REQ);
    zmq_setsockopt(client, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(client, ZMQ_CURVE_SERVERKEY, server_pub, 40);
    zmq_setsockopt(client, ZMQ_CURVE_PUBLICKEY, client_pub, 40);
    zmq_setsockopt(client, ZMQ_CURVE_SECRETKEY, client_sec, 40);
    zmq_connect(client, "tcp://127.0.0.1:15992");

    std::cout << "client permanent public key (Z85): " << client_pub << "\n";
    zmq_send(client, "whoami", 6, 0);

    std::cout << "server receiving...\n";
    std::string frames[8];
    int nf = 0;
    zmq_pollitem_t sitems[] = {{server, 0, ZMQ_POLLIN, 0}};
    if (zmq_poll(sitems, 1, 4000) > 0) {
        for (;;) {
            zmq_msg_t m;
            zmq_msg_init(&m);
            int n = zmq_msg_recv(&m, server, 0);
            if (n < 0) {
                zmq_msg_close(&m);
                break;
            }
            int more = zmq_msg_more(&m);
            std::string data((const char *)zmq_msg_data(&m), zmq_msg_size(&m));
            zmq_msg_close(&m);

            if (nf < 8) frames[nf] = data;
            std::cout << "  frame[" << nf << "] (" << data.size() << " bytes) hex=";
            for (size_t i = 0; i < data.size() && i < 16; i++) {
                char b[4];
                snprintf(b, sizeof(b), "%02x", (unsigned char)data[i]);
                std::cout << b;
            }
            if (data.size() <= 48) std::cout << " ascii=\"" << data << "\"";
            std::cout << "\n";
            nf++;
            if (!more) break;
        }
    } else {
        std::cout << "  no message received\n";
    }

    // The CURVE-authenticated peer identity is the client's raw 32-byte public
    // key, not a text User-Id. Compare against the client's real key bytes.
    bool sawRawKey = false;
    for (int i = 0; i < nf && i < 8; i++) {
        if (frames[i].size() == 32 && frames[i][0] == (char)(unsigned char)0x67 &&
            frames[i][1] == (char)(unsigned char)0x35) {
            sawRawKey = true;
        }
    }
    std::cout << (sawRawKey
                      ? "RESULT: identity frame IS the client's raw 32-byte CURVE public key\n"
                      : "RESULT: no raw-key identity frame found\n");

    zmq_close(client);
    zmq_close(server);
    stop.store(true);
    zap.join();
    zmq_ctx_term(ctx);
    return 0;
}
