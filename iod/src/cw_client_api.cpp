#include <cw_client/cw_client.h>

#include <memory>
#include <sstream>
#include <list>
#include "MessageEncoding.h"
#include "value.h"
#include <zmq.hpp>

namespace cw_client {

class Client::Impl {
public:
    Impl(const std::string &host, unsigned short port, int timeout)
        : endpoint("tcp://" + host + ":" + std::to_string(port)), timeout_ms(timeout) {
        reset();
    }

    void reset() {
        socket.reset(new zmq::socket_t(context, ZMQ_REQ));
        int linger = 0;
        socket->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        socket->setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
        socket->setsockopt(ZMQ_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
        socket->connect(endpoint.c_str());
    }

    zmq::context_t context{1};
    std::unique_ptr<zmq::socket_t> socket;
    std::string endpoint;
    int timeout_ms;
};

Client::Client(std::string host, unsigned short port, int timeout_ms)
    : impl_(new Impl(host, port, timeout_ms > 0 ? timeout_ms : 2000)) {}
Client::~Client() { delete impl_; }
void Client::reconnect() { impl_->reset(); }

Reply Client::request(const std::string &command) {
    Reply result;
    try {
        zmq::message_t message(command.size());
        if (!command.empty()) memcpy(message.data(), command.data(), command.size());
        if (!impl_->socket->send(message)) {
            result.timed_out = true;
            result.error = "IOD send timed out";
            reconnect();
            return result;
        }
        zmq::message_t reply;
        if (!impl_->socket->recv(&reply)) {
            result.timed_out = true;
            result.error = "IOD reply timed out";
            reconnect();
            return result;
        }
        result.ok = true;
        result.text.assign(static_cast<const char *>(reply.data()), reply.size());
    } catch (const zmq::error_t &error) {
        result.error = error.what();
        result.timed_out = zmq_errno() == EAGAIN;
        reconnect();
    }
    return result;
}

Reply Client::request_command(const std::string &command,
                              const std::vector<std::string> &arguments) {
    std::list<Value> values;
    for (const std::string &argument : arguments) values.push_back(Value{argument});
    return request(MessageEncoding::encodeCommand(command, values));
}

}  // namespace cw_client
