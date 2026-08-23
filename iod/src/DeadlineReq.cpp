#include "DeadlineReq.h"
#include "MessagingInterface.h"
#include <iostream>
#include <string.h>

static void setLinger0(zmq::socket_t &sock) {
    int linger = 0;
    try {
        sock.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    }
    catch (const zmq::error_t &) {
    }
}

DeadlineReq::DeadlineReq(zmq::context_t &ctx, const std::string &endpoint)
    : ctx_(&ctx), endpoint_(endpoint), sock_(0) {
    createSocket();
}

DeadlineReq::~DeadlineReq() { delete sock_; }

void DeadlineReq::createSocket() {
    delete sock_;
    sock_ = new zmq::socket_t(*ctx_, ZMQ_REQ);
    setLinger0(*sock_);
    sock_->connect(endpoint_.c_str());
}

void DeadlineReq::reconnect() { createSocket(); }

bool DeadlineReq::request(const std::string &msg, std::string &reply, int64_t timeout_ms) {
    if (!sock_) {
        createSocket();
    }
    try {
        safeSend(*sock_, msg.c_str(), msg.size());
    }
    catch (const zmq::error_t &) {
        std::cerr << "DeadlineReq send failed: " << zmq_strerror(zmq_errno()) << "\n";
        createSocket();
        return false;
    }

    const uint64_t deadline = microsecs() + (uint64_t)timeout_ms * 1000ULL;
    while (microsecs() < deadline) {
        int64_t remain_ms = (int64_t)((deadline - microsecs()) / 1000ULL);
        if (remain_ms < 1) {
            remain_ms = 1;
        }
        if (remain_ms > 200) {
            remain_ms = 200;
        }
        try {
            zmq::pollitem_t items[] = {{(void *)*sock_, 0, ZMQ_POLLIN, 0}};
            int n = zmq::poll(items, 1, (long)remain_ms);
            if (n > 0 && (items[0].revents & ZMQ_POLLIN)) {
                char *buf = nullptr;
                size_t len = 0;
                if (safeRecv(*sock_, &buf, &len, false, 0)) {
                    reply = buf ? buf : "";
                    delete[] buf;
                    return true;
                }
            }
        }
        catch (const zmq::error_t &) {
            if (zmq_errno() == EINTR) {
                continue;
            }
            std::cerr << "DeadlineReq recv failed: " << zmq_strerror(zmq_errno()) << "\n";
            createSocket();
            return false;
        }
    }
    std::cerr << "DeadlineReq timed out after " << timeout_ms << "ms (" << endpoint_ << ")\n";
    createSocket();
    return false;
}
