#pragma once

#include <string>
#include <zmq.hpp>

// Optional CurveZMQ settings for ONE connection.
//
// CURVE is configured per connection, never per process. dbd holds two
// DeadlineReq objects - the local iod channel and the datastore - and only the
// datastore one may ever speak CURVE. Applying options globally (for example
// inside createSocket without regard to which endpoint it is building) would
// break the local iod channel, which is not a CURVE peer, and would equally break
// dbd against a plain-ZMQ dbsvr. So the settings live on the instance and are
// re-applied on every reconnect.
//
// An empty serverKey means "plain ZMQ": no CURVE option is set at all, which is
// the default and leaves existing deployments unchanged.
struct CurveOptions {
    // The peer's long-term public key, Z85 encoded. This is the whole trust
    // anchor for CURVE - there is no CA and no certificate.
    std::string serverKey;
    // This client's own keypair, Z85 encoded.
    std::string clientPublicKey;
    std::string clientSecretKey;

    bool enabled() const { return !serverKey.empty(); }
};

// Long-lived ZMQ_REQ with linger 0 and a recv deadline. Recreates the socket
// after timeout or EFSM so a peer restart does not wedge the client.
class DeadlineReq {
  public:
    DeadlineReq(zmq::context_t &ctx, const std::string &endpoint);
    DeadlineReq(zmq::context_t &ctx, const std::string &endpoint,
                const CurveOptions &curve);
    ~DeadlineReq();

    bool request(const std::string &msg, std::string &reply, int64_t timeout_ms);
    void reconnect();
    const std::string &endpoint() const { return endpoint_; }
    const CurveOptions &curveOptions() const { return curve_; }
    // True when this connection was built with CURVE enabled. A plain connection
    // must report false; tests assert that the iod channel never becomes CURVE.
    bool curveEnabled() const { return curve_.enabled(); }

  private:
    DeadlineReq(const DeadlineReq &);
    DeadlineReq &operator=(const DeadlineReq &);
    void createSocket();
    void applyCurveOptions();

    zmq::context_t *ctx_;
    std::string endpoint_;
    zmq::socket_t *sock_;
    CurveOptions curve_;
};
