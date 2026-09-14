// Tests for per-connection CurveZMQ configuration on DeadlineReq.
//
// Why this exists
// ---------------
// dbd holds TWO DeadlineReq objects: the local iod channel and the datastore.
// Both are built by the same socket factory, and reconnect() re-enters it. If
// CURVE were applied globally - the shape PLAN.md section 4.5 originally implied
// - enabling it would break the local iod channel (not a CURVE peer) and would
// also break dbd against a plain-ZMQ dbsvr. That is the datastore-side regression
// this file guards: enabling CURVE for one connection must not touch the other.
//
// Self-contained: no site paths, no external services. See iod/AGENTS.md.

#include "DeadlineReq.h"
#include "MessagingInterface.h"
#include <iostream>
#include <string>
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

int main() {
    zmq::context_t ctx;
    MessagingInterface::setContext(&ctx);

    const char *plain_ep = "tcp://127.0.0.1:15861";
    const char *curve_ep = "tcp://127.0.0.1:15862";

    // A well-formed Z85 keypair is required for CURVE to be applied at all, so the
    // test generates real keys rather than using placeholder strings.
    char server_pub[41] = {0};
    char server_sec[41] = {0};
    char client_pub[41] = {0};
    char client_sec[41] = {0};

    zmq::socket_t curve_server(ctx, ZMQ_REP);
    try {
        // Real Z85 keypairs: CURVE rejects malformed keys, and a zero-filled
        // buffer is not a valid key.
        if (zmq_curve_keypair(server_pub, server_sec) != 0 ||
            zmq_curve_keypair(client_pub, client_sec) != 0) {
            std::cerr << "could not generate CURVE keypairs\n";
            return 1;
        }

        curve_server.setsockopt(ZMQ_CURVE_SERVER, 1);
        curve_server.setsockopt(ZMQ_CURVE_PUBLICKEY, server_pub, 40);
        curve_server.setsockopt(ZMQ_CURVE_SECRETKEY, server_sec, 40);
    }
    catch (const zmq::error_t &e) {
        std::cerr << "libzmq lacks CurveZMQ support: " << e.what() << "\n";
        return 0;
    }

    // 1. The iod-style plain connection must not be CURVE, and must stay plain
    //    across a reconnect. This is the core regression guard.
    {
        DeadlineReq iod_req(ctx, plain_ep);
        check(!iod_req.curveEnabled(), "default connection is plain ZMQ");
        check(iod_req.curveOptions().serverKey.empty(),
              "default connection carries no server key");

        iod_req.reconnect();
        check(!iod_req.curveEnabled(), "plain connection stays plain after reconnect");
    }

    // 2. A CURVE-configured connection must report itself as such, including after
    //    a reconnect re-enters the socket factory.
    {
        CurveOptions opts;
        opts.serverKey = std::string(server_pub, 40);
        opts.clientPublicKey = std::string(client_pub, 40);
        opts.clientSecretKey = std::string(client_sec, 40);

        DeadlineReq dbsvr_req(ctx, curve_ep, opts);
        check(dbsvr_req.curveEnabled(), "CURVE-configured connection reports enabled");
        check(dbsvr_req.curveOptions().serverKey == opts.serverKey,
              "CURVE options are retained on the instance");

        dbsvr_req.reconnect();
        check(dbsvr_req.curveEnabled(),
              "CURVE connection stays CURVE after reconnect");
    }

    // 3. Enabling CURVE with no client keypair must fail loudly, never connect in
    //    the clear. A silent downgrade would defeat the purpose of enabling it.
    {
        CurveOptions incomplete;
        incomplete.serverKey = std::string(server_pub, 40);
        bool threw = false;
        try {
            DeadlineReq bad(ctx, curve_ep, incomplete);
        }
        catch (const std::exception &) {
            threw = true;
        }
        check(threw, "missing client keypair is refused rather than downgraded");
    }

    // 4. The two connections are independent: a CURVE one must not make a
    //    subsequent plain one CURVE. Order matters - this is the cross-contamination
    //    case that a global option would cause.
    {
        CurveOptions opts;
        opts.serverKey = std::string(server_pub, 40);
        opts.clientPublicKey = std::string(client_pub, 40);
        opts.clientSecretKey = std::string(client_sec, 40);

        DeadlineReq a(ctx, curve_ep, opts);
        DeadlineReq b(ctx, plain_ep);
        check(a.curveEnabled() && !b.curveEnabled(),
              "CURVE and plain connections coexist without contaminating each other");
    }

    curve_server.close();

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all DeadLineReq curve configuration checks passed\n";
    return 0;
}
