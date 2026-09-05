#pragma once

#include <string>
#include <zmq.hpp>

// Long-lived ZMQ_REQ with linger 0 and a recv deadline. Recreates the socket
// after timeout or EFSM so a peer restart does not wedge the client.
class DeadlineReq {
  public:
    DeadlineReq(zmq::context_t &ctx, const std::string &endpoint);
    ~DeadlineReq();

    bool request(const std::string &msg, std::string &reply, int64_t timeout_ms);
    void reconnect();
    const std::string &endpoint() const { return endpoint_; }

  private:
    DeadlineReq(const DeadlineReq &);
    DeadlineReq &operator=(const DeadlineReq &);
    void createSocket();

    zmq::context_t *ctx_;
    std::string endpoint_;
    zmq::socket_t *sock_;
};
