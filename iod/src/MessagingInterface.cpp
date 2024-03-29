/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc

    Latproc is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    Latproc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Latproc; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "MessagingInterface.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "cJSON.h"
#include <assert.h>
#include <exception>
#include <iostream>
#include <map>
#include <math.h>
#include <zmq.hpp>
#ifdef DYNAMIC_VALUES
#include "dynamic_value.h"
#endif
#include "Channel.h"
#include "Dispatcher.h"
#include "MessageEncoding.h"
#include "MessageLog.h"
#include "anet.h"
#include "options.h"
#include "string.h"
#include "symboltable.h"

const char *program_name;
static std::string STATE_ERROR("Operation cannot be accomplished in current state");

MessagingInterface *MessagingInterface::current = 0;
zmq::context_t *MessagingInterface::zmq_context = 0;
std::map<std::string, MessagingInterface *> MessagingInterface::interfaces;
bool MessagingInterface::abort_all = false;

uint64_t nowMicrosecs() { return microsecs(); }

#if 0
uint64_t nowMicrosecs(const struct timeval &now)
{
    return (uint64_t)now.tv_sec * 1000000L + (uint64_t)now.tv_usec;
}

int64_t get_diff_in_microsecs(const struct timeval *now, const struct timeval *then)
{
    uint64_t now_t = (uint64_t)now->tv_sec * 1000000L + now->tv_usec;
    uint64_t then_t = (uint64_t)then->tv_sec * 1000000L + then->tv_usec;
    int64_t t = now_t - then_t;
    return t;
}

int64_t get_diff_in_microsecs(uint64_t now_t, const struct timeval *then)
{
    uint64_t then_t = (uint64_t)then->tv_sec * 1000000L + then->tv_usec;
    int64_t t = now_t - then_t;
    return t;
}

int64_t get_diff_in_microsecs(const struct timeval *now, uint64_t then_t)
{
    uint64_t now_t = (uint64_t)now->tv_sec * 1000000L + now->tv_usec;
    int64_t t = now_t - then_t;
    return t;
}
#endif

zmq::context_t *MessagingInterface::getContext() { return zmq_context; }

static std::string thread_name() {
    char tnam[100];
    int pgn_rc = pthread_getname_np(pthread_self(), tnam, 100);
    assert(pgn_rc == 0);
    return tnam;
}

bool safeRecv(zmq::socket_t &sock, char **buf, size_t *response_len, bool block, int64_t timeout) {
    *response_len = 0;
    if (block && timeout == 0) {
        timeout = 500;
    }

    while (!MessagingInterface::aborted()) {
        try {
            zmq::pollitem_t items[] = {{(void *)sock, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0}};
            int n = zmq::poll(&items[0], 1, timeout);
            if (!n && block) {
                continue;
            }
            if (items[0].revents & ZMQ_POLLERR) {
                std::cerr << thread_name() << " safeRecv error " << errno << " "
                          << zmq_strerror(errno) << "\n";
            }
            if (items[0].revents & ZMQ_POLLIN) {
                bool done = false;
                zmq::message_t message;
                while (!done) {
                    {
                        if ((sock.recv(&message, ZMQ_DONTWAIT))) {
                            *response_len = message.size();
                            *buf = new char[*response_len + 1];
                            memcpy(*buf, message.data(), *response_len);
                            (*buf)[*response_len] = 0;
                            return true;
                        }
                        else {
                            if (!block) {
                                done = true;
                            }
                        }
                    }
                }
            }
            return (*response_len == 0) ? false : true;
        }
        catch (const zmq::error_t &e) {
            std::cerr << thread_name() << " safeRecv error " << errno << " " << zmq_strerror(errno)
                      << "\n";
            if (errno == EINTR) {
                {
                    FileLogger fl(program_name);
                    fl.f() << "safeRecv interrupted system call, retrying\n";
                }
                if (block) {
                    continue;
                }
            }
            usleep(10);
            return false;
        }
    }
    return false;
}

bool safeRecv(zmq::socket_t &sock, char **buf, size_t *response_len, bool block, int64_t timeout,
              MessageHeader &header) {
    *response_len = 0;
    if (block && timeout == 0) {
        timeout = 500;
    }

    while (!MessagingInterface::aborted()) {
        try {
            zmq::pollitem_t items[] = {{(void *)sock, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0}};
            int n = zmq::poll(&items[0], 1, timeout);
            if (!n && block) {
                continue;
            }
            bool got_response = false;
            if (items[0].revents & ZMQ_POLLIN) {

                bool done = false;
                zmq::message_t message;
                while (!done) {
                    if ((got_response = sock.recv(&message, ZMQ_DONTWAIT))) {
                        if (message.more() && message.size() == sizeof(MessageHeader)) {
                            memcpy(&header, message.data(), sizeof(MessageHeader));
                            continue;
                        }
                        *response_len = message.size();
                        *buf = new char[*response_len + 1];
                        memcpy(*buf, message.data(), *response_len);
                        (*buf)[*response_len] = 0;
                        return true;
                    }
                    else {
                        if (!block) {
                            done = true;
                        }
                    }
                }
                return (*response_len == 0) ? false : true;
            }
            else {
                return false;
            }
        }
        catch (const zmq::error_t &e) {
            std::cerr << thread_name() << " safeRecv error " << errno << " " << zmq_strerror(errno)
                      << "\n";
            if (errno == EINTR) {
                {
                    FileLogger fl(program_name);
                    fl.f() << "safeRecv interrupted system call, retrying\n";
                }
                if (block) {
                    continue;
                }
            }
            usleep(10);
            return false;
        }
    }
    if (MessagingInterface::aborted()) {
        FileLogger fl(program_name);
        fl.f() << "messaging interface aborted\n";
    }
    return false;
}

bool safeRecv(zmq::socket_t &sock, char *buf, int buflen, bool block, size_t &response_len,
              int64_t timeout) {
    response_len = 0;
    int retries = 5;
    if (block && timeout == 0) {
        timeout = 500;
    }
    while (!MessagingInterface::aborted()) {
        try {
            zmq::pollitem_t items[] = {{(void *)sock, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0}};
            int n = zmq::poll(&items[0], 1, timeout);
            if (!n && block) {
                usleep(10);
                continue;
            }
            if (items[0].revents & ZMQ_POLLIN) {
                response_len = sock.recv(buf, buflen, ZMQ_DONTWAIT);
                if (response_len > 0 && response_len < (unsigned int)buflen) {
                    buf[response_len] = 0;
                }
                if (!response_len && block) {
                    continue;
                }
            }
            return (response_len == 0) ? false : true;
        }
        catch (const zmq::error_t &e) {
            {
                FileLogger fl(program_name);
                fl.f() << thread_name() << " safeRecv error " << errno << " " << zmq_strerror(errno)
                       << "\n";
            }
            if (--retries == 0) {
                exit(EXIT_FAILURE);
            }
            if (errno == EINTR) {
                std::cerr << "interrupted system call, retrying\n";
                if (block) {
                    continue;
                }
            }
            usleep(10);
            return false;
        }
    }
    return false;
}

void safeSend(zmq::socket_t &sock, const char *buf, size_t buflen, const MessageHeader &header) {
    enum send_stage { e_sending_dest, e_sending_source, e_sending_data } stage = e_sending_data;
    if (header.dest || header.source) {
        stage = e_sending_source;
    }
    assert(header.start_time != 0);

    while (!MessagingInterface::aborted()) {
        try {
            if (stage == e_sending_source) {
                zmq::message_t msg(sizeof(MessageHeader));
                memcpy(msg.data(), &header, sizeof(MessageHeader));
                sock.send(msg, ZMQ_SNDMORE);
                stage = e_sending_data;
            }
            if (stage == e_sending_data) {
                zmq::message_t msg(buflen);
                memcpy(msg.data(), buf, buflen);
                sock.send(msg);
            }
            break;
        }
        catch (const zmq::error_t &) {
            if (zmq_errno() != EINTR && zmq_errno() != EAGAIN) {
                {
                    FileLogger fl(program_name);
                    fl.f() << thread_name() << " safeSend error " << errno << " "
                           << zmq_strerror(errno) << "\n";
                }
                if (zmq_errno() == EFSM || STATE_ERROR == zmq_strerror(errno)) {
                    throw;
                }
                usleep(10);
                continue;
            }
            else {
                {
                    FileLogger fl(program_name);
                    fl.f() << thread_name() << " safeSend error " << errno << " "
                           << zmq_strerror(errno) << "\n";
                }
                usleep(10);
            }
        }
    }
    if (MessagingInterface::aborted()) {
        FileLogger fl(program_name);
        fl.f() << " messaging interface is aborted\n";
    }
}

void safeSend(zmq::socket_t &sock, const char *buf, size_t buflen) {
    while (!MessagingInterface::aborted()) {
        try {
            zmq::message_t msg(buflen);
            memcpy(msg.data(), buf, buflen);
            sock.send(msg);
            break;
        }
        catch (const zmq::error_t &) {
            if (zmq_errno() != EINTR && zmq_errno() != EAGAIN) {
                {
                    FileLogger fl(program_name);
                    fl.f() << thread_name() << " safeSend error " << errno << " "
                           << zmq_strerror(errno) << "\n";
                }
                if (zmq_errno() == EFSM || STATE_ERROR == zmq_strerror(errno)) {
                    usleep(1000);
                    throw;
                }
                usleep(10);
                continue;
            }
            else {
                std::cerr << thread_name() << " safeSend error " << errno << " "
                          << zmq_strerror(errno) << "\n";
                usleep(10);
            }
        }
    }
}

bool sendMessage(const char *msg, zmq::socket_t &sock, std::string &response, int32_t timeout_us,
                 const MessageHeader &header) {
    safeSend(sock, msg, strlen(msg), header);

    char *buf;
    size_t len;
    MessageHeader response_header;
    //bool safeRecv(zmq::socket_t &sock, char **buf, size_t *response_len, bool block, uint64_t timeout, int *source) {
    if (safeRecv(sock, &buf, &len, true, (int64_t)timeout_us, response_header)) {
        response = buf;
        delete[] buf;
        return true;
    }
    return false;
}

bool sendMessage(const char *msg, zmq::socket_t &sock, std::string &response, int32_t timeout_us) {
    safeSend(sock, msg, strlen(msg));

    char *buf = 0;
    size_t len = 0;
    if (safeRecv(sock, &buf, &len, true, (int64_t)timeout_us)) {
        response = buf;
        delete[] buf;
        return true;
    }
    return false;
}

void MessagingInterface::setContext(zmq::context_t *ctx) {
    zmq_context = ctx;
    assert(zmq_context);
}

MessagingInterface *MessagingInterface::create(std::string host, int port, ProtocolType proto) {
    std::stringstream ss;
    ss << host << ":" << port;
    std::string id = ss.str();
    if (interfaces.count(id) == 0) {
        DBG_CHANNELS << "creating new interface to " << id << "\n";
        MessagingInterface *res =
            new MessagingInterface(host, port, MessagingInterface::IMMEDIATE_START, proto);
        interfaces[id] = res;
        return res;
    }
    else {
        DBG_CHANNELS << "returning existing interface to " << id << "\n";
        return interfaces[id];
    }
}

MessagingInterface::MessagingInterface(int num_threads, int port_, bool deferred_start,
                                       ProtocolType proto)
    : Receiver("messaging_interface"), protocol(proto), socket(0), is_publisher(false),
      connection(-1), port(port_), owner_thread(0), started_(false) {
    owner_thread = pthread_self();
    is_publisher = true;
    hostname = "*";
    std::stringstream ss;
    ss << "tcp://" << hostname << ":" << port;
    url = ss.str();
    if (protocol == eCHANNEL) {
        socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PAIR);
    }
    else {
        socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PUB);
    }

    if (!deferred_start) {
        start();
    }
}

void MessagingInterface::start() {
    if (started_) {
        DBG_CHANNELS << "Messaging interface already started; skipping\n";
        return;
    }
    if (protocol == eCLOCKWORK || protocol == eZMQ || protocol == eCHANNEL) {
        owner_thread = pthread_self();
        if (hostname == "*" || hostname == "0.0.0.0") {
            NB_MSG << "binding " << url << "\n";
            socket->bind(url.c_str());
            started_ = true;
        }
        else {
            connect();
        }
    }
    else {
        connect();
    }
}

void MessagingInterface::stop() { started_ = false; }

bool MessagingInterface::started() { return started_; }

MessagingInterface::MessagingInterface(std::string host, int remote_port, bool deferred,
                                       ProtocolType proto)
    : Receiver("messaging_interface"), protocol(proto), socket(0), is_publisher(false),
      connection(-1), hostname(host), port(remote_port), owner_thread(0), started_(false) {
    std::stringstream ss;
    ss << "tcp://" << host << ":" << port;
    url = ss.str();
    if (protocol == eCLOCKWORK || protocol == eZMQ || protocol == eCHANNEL) {
        if (hostname == "*" || hostname == "0.0.0.0") {
            if (protocol == eCHANNEL) {
                is_publisher = true; // TBD
                socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PAIR);
            }
            else {
                is_publisher = true;
                socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PUB);
            }
        }
        else {
            is_publisher = false; // TBD need to support acks
            socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
        }
    }
    else if (protocol != eRAW) {
        NB_MSG << "Warning: unexpected protocol " << protocol
               << " constructing a messaging interface on " << url << "\n";
    }

    if (!deferred) {
        start();
    }
}

int MessagingInterface::uniquePort(unsigned int start, unsigned int end) {
    int res = 0;
    char address_buf[40];
    std::set<unsigned int> checked_ports; // used to avoid duplicate checks and infinite loops
    while (true) {
        try {
            zmq::socket_t test_bind(*MessagingInterface::getContext(), ZMQ_PULL);
            res = random() % (end - start + 1) + start;
            if (checked_ports.count(res) && checked_ports.size() < end - start + 1) {
                continue;
            }
            checked_ports.insert(res);
            snprintf(address_buf, 40, "tcp://0.0.0.0:%d", res);
            test_bind.bind(address_buf);
            int linger = 0; // do not wait at socket close time
            test_bind.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
            test_bind.disconnect(address_buf);
            DBG_CHANNELS << "found available port " << res << "\n";
            break;
        }
        catch (const zmq::error_t &err) {
            if (zmq_errno() != EADDRINUSE) {
                res = 0;
                break;
            }
            if (checked_ports.size() == end - start + 1) {
                res = 0;
                break;
            }
        }
    }
    return res;
}

void MessagingInterface::connect() {
    if (protocol == eCLOCKWORK || protocol == eZMQ || protocol == eCHANNEL) {
        if (pthread_equal(owner_thread, pthread_self())) {
            FileLogger fl(program_name);
            fl.f() << hostname << ":" << port
                   << " attempt to call socket connect from a thread that isn't the owner\n";
            // return; // no longer returning here, we have some evidence this is not a good test. TBD
        }
        DBG_CHANNELS << "calling connect on " << url << "\n";
        socket->connect(url.c_str());
        int linger = 0;
        socket->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    }
    else {
        char error[ANET_ERR_LEN];
        connection = anetTcpConnect(error, hostname.c_str(), port);
        if (connection == -1) {
            MessageLog::instance()->add(error);
            DBG_CHANNELS << "MessagingInterface::connect error " << error << "\n";
        }
    }
    started_ = true;
}

MessagingInterface::~MessagingInterface() {
    int retries = 3;
    while (retries-- && connection != -1) {
        int err = close(connection);
        if (err == -1 && errno == EINTR) {
            continue;
        }
        connection = -1;
    }
    if (MessagingInterface::current == this) {
        MessagingInterface::current = 0;
    }
    socket->disconnect(url.c_str());
    delete socket;
}

bool MessagingInterface::receives(const Message &, Transmitter *t) { return true; }
void MessagingInterface::handle(const Message &msg, Transmitter *from, bool needs_receipt) {
    char *response = send(msg);
    if (response) {
        free(response);
    }
}

char *MessagingInterface::send(const char *txt) {
    if (owner_thread != pthread_self()) {
        char tnam1[100], tnam2[100];
        int pgn_rc = pthread_getname_np(pthread_self(), tnam1, 100);
        if (pgn_rc != 0) {
            NB_MSG << "Warning: Error code " << pgn_rc << " returned when getting thread name\n";
        }
        pgn_rc = pthread_getname_np(owner_thread, tnam2, 100);
        if (pgn_rc != 0) {
            NB_MSG << "Warning: Error code " << pgn_rc << " returned when getting thread name\n";
        }

        NB_MSG << "error: message send (" << txt << ") from a different thread:"
               << " owner: " << std::hex << " '" << owner_thread << " '" << tnam2
               << "' current: " << std::hex << " " << pthread_self() << " '" << tnam1 << "'"
               << std::dec << "\n";
    }

    if (!is_publisher) {
        DBG_MESSAGING << getName() << " sending message " << txt << " on " << url << "\n";
    }
    else {
        DBG_MESSAGING << getName() << " sending message " << txt << "\n";
    }
    size_t len = strlen(txt);

    // We try to send a few times and if an exception occurs we try a reconnect
    // but is this useful without a sleep in between?
    // It seems unlikely the conditions will have changed between attempts.
    zmq::message_t msg(len);
    strncpy((char *)msg.data(), txt, len);
    int fsm_retries = 1;
    int retries = 3;
    enum { e_send, e_recover } send_state = e_send;
    while (true) {
        try {
            if (send_state == e_send) {
                socket->send(msg);
                break;
            }
            else {
                zmq::message_t m;
                socket->recv(&m, ZMQ_DONTWAIT);
                send_state = e_send;
                continue;
            }
        }
        catch (const std::exception &e) {
            if (errno == EINTR || errno == EAGAIN) {
                std::cerr << "MessagingInterface::send " << strerror(errno);
                if (--retries <= 0) {
                    FileLogger fl(program_name);
                    fl.f() << "MessagingInterface::send aborting after too many errors\n";
                    exit(2);
                }
                continue;
            }
            if (zmq_errno() == EFSM) {
                if (--fsm_retries <= 0) {
                    FileLogger fl(program_name);
                    fl.f() << "MessagingInterface::send aborting due to failed EFSM recovery\n";
                    exit(2);
                }
                send_state = e_recover;
                continue;
            }
            if (zmq_errno()) {
                FileLogger fl(program_name);
                fl.f() << "Exception when sending " << url << ": " << zmq_strerror(zmq_errno())
                       << "\n";
            }
            else {
                FileLogger fl(program_name);
                fl.f() << "Exception when sending " << url << ": " << e.what() << "\n";
            }
            /*          socket->disconnect(url.c_str());
                    usleep(50000);
                    connect();
            */
            exit(2);
        }
    }
    if (!is_publisher) {
        while (true) {
            try {
                zmq::pollitem_t items[] = {{(void *)*socket, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0}};
                int n = zmq::poll(&items[0], 1, 10);
                if (n == 0) {
                    continue;
                }
                if (n == 1 && items[0].revents & ZMQ_POLLIN) {
                    zmq::message_t reply;
                    if (socket->recv(&reply)) {
                        len = reply.size();
                        char *data = (char *)malloc(len + 1);
                        memcpy(data, reply.data(), len);
                        data[len] = 0;
                        std::cerr << url << ": " << data << "\n";
                        return data;
                    }
                    else {
                        usleep(100);
                        continue;
                    }
                }
                else if (items[0].revents & ZMQ_POLLERR) {
                    std::cerr << "MessagingInterface::send: error during recv\n";
                    continue;
                }
                break;
            }
            catch (const std::exception &e) {
                if (zmq_errno()) {
                    std::cerr << "Exception when receiving response " << url << ": "
                              << zmq_strerror(zmq_errno()) << "\n";
                }
                else {
                    std::cerr << "Exception when receiving response " << url << ": " << e.what()
                              << "\n";
                }
            }
        }
    }
    return 0;
}

char *MessagingInterface::send(const Message &msg) {
    char *text = MessageEncoding::encodeCommand(msg.getText(), msg.getParams());
    char *res = send(text);
    free(text);
    return res;
}

bool MessagingInterface::send_raw(const char *msg) {
    char error[ANET_ERR_LEN];
    int retry = 2;
    size_t len = strlen(msg);
    while (retry--) {
        size_t written = anetWrite(connection, (char *)msg, len);
        if (written != len) {
            snprintf(error, ANET_ERR_LEN, "error sending message: %s", msg);
            MessageLog::instance()->add(error);
            close(connection);
            connect();
        }
        else {
            return true;
        }
    }
    if (!retry) {
        return false;
    }
    return true;
}
char *MessagingInterface::sendCommand(std::string cmd, std::list<Value> *params) {
    char *request = MessageEncoding::encodeCommand(cmd, params);
    char *response = send(request);
    free(request);
    return response;
}

/*  TBD, this may need to be optimised, one approach would be to use a
    templating system; a property message looks like this:

    { "command":    "PROPERTY", "params":
    [
         { "type":  "NAME", "value":    $1 },
         { "type":   "NAME", "value":   $2 },
         { "type":  TYPEOF($3), "value":  $3 }
     ] }

*/

char *MessagingInterface::sendCommand(std::string cmd, Value p1, Value p2, Value p3) {
    std::list<Value> params;
    params.push_back(p1);
    params.push_back(p2);
    params.push_back(p3);
    return sendCommand(cmd, &params);
}

char *MessagingInterface::sendState(std::string cmd, std::string name, std::string state_name) {
    size_t msglen = name.length() + state_name.length() + 50;
    char *buf = (char *)malloc(msglen);
    snprintf(buf, msglen, "{\"command\":\"STATE\", \"params\":[\"%s\", \"%s\"]}", name.c_str(),
             state_name.c_str());
    return buf;
    /*
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "command", cmd.c_str());
        cJSON *cjParams = cJSON_CreateArray();
        cJSON_AddStringToObject(cjParams, "name", name.c_str());
        cJSON_AddStringToObject(cjParams, "state", state_name.c_str());
        cJSON_AddItemToObject(msg, "params", cjParams);
        char *request = cJSON_PrintUnformatted(msg);
        cJSON_Delete(msg);
        char *response = send(request);
        free (request);
        return response;
    */
}
