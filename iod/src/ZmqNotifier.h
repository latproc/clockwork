#pragma once
#include <zmq.hpp>
#include <string>

class ZmqNotifier {
public:
    ZmqNotifier(zmq::context_t& context, const std::string& endpoint)
        : socket_(context, ZMQ_PUSH) {
        socket_.connect(endpoint);
    }

    void operator()() const {
        zmq::message_t msg("non_empty", 9);
        socket_.send(msg, ZMQ_DONTWAIT);
    }

private:
    mutable zmq::socket_t socket_;
};

