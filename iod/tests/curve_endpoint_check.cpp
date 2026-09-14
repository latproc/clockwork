// Drives the real datastore client - DeadlineReq, which is what dbd uses for
// --dbsvr - against an ALREADY RUNNING CurveZMQ server.
//
// Why this exists alongside the other CURVE tests:
//
//   test_deadline_req_curve       proves the options are set per connection;
//   test_deadline_req_curve_wire  proves they take effect against a server this
//                                 test starts itself, in-process;
//   this program                  proves the client interoperates with a real
//                                 DEPLOYED peer - a different implementation,
//                                 built and configured separately.
//
// The third case is not covered by the other two, and it is the one that catches
// interop mistakes: a shared header cannot hide a framing difference between two
// implementations that each pass their own tests. This is not a CTest, because it
// needs a peer the test cannot create, so everything is an argument and it is run
// by hand against a target.
//
// Usage:
//   curve_endpoint_check <endpoint> <server-public-key> \
//                        <client-public-key> <client-secret-key> <request-json>
//
// The request is supplied by the caller deliberately: this is product code and
// carries no deployment's relation names, table names or database paths.
//
// Exit status: 0 when a reply arrived; 1 when it did not, which is also what a
// refused handshake looks like from here - ZAP refuses by closing the connection,
// so there is no reply and no error; 2 on misuse.

#include "DeadlineReq.h"
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    if (argc < 6) {
        std::cerr << "usage: " << argv[0]
                  << " <endpoint> <server-public-key> <client-public-key>"
                     " <client-secret-key> <request-json>\n"
                     "\n"
                     "  Drives the real datastore client (DeadlineReq) against a running\n"
                     "  CurveZMQ peer. The peer's public key is the trust anchor; the other\n"
                     "  two are this client's identity, which the peer authorises by its own\n"
                     "  means. A refusal is silent, so exit 1 means \"refused or unreachable\".\n";
        return 2;
    }

    const std::string endpoint = argv[1];

    // CURVE is per connection, exactly as dbd configures its datastore socket.
    CurveOptions curve;
    curve.serverKey = argv[2];
    curve.clientPublicKey = argv[3];
    curve.clientSecretKey = argv[4];

    const std::string request = argv[5];

    zmq::context_t ctx(1);
    DeadlineReq dbsvr(ctx, endpoint, curve);

    std::cout << "endpoint: " << dbsvr.endpoint() << "\n";
    std::cout << "curve enabled: " << (dbsvr.curveEnabled() ? "yes" : "no") << "\n";
    std::cout << "request: " << request << "\n";

    std::string reply;
    const int64_t timeout_ms = 15000;
    if (!dbsvr.request(request, reply, timeout_ms)) {
        std::cout << "RESULT: FAILED (no reply within " << timeout_ms
                  << " ms - refused during the handshake, or unreachable)\n";
        return 1;
    }

    std::cout << "RESULT: OK\n";
    std::cout << "reply: " << reply << "\n";
    return 0;
}
