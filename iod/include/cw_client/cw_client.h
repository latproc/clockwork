#pragma once

#include <string>
#include <vector>

namespace cw_client {

struct Reply {
    bool ok = false;
    bool timed_out = false;
    std::string text;
    std::string error;
};

/** A small synchronous client for the IOD ZeroMQ request socket.
 *
 * A Client owns no daemon state.  Each failed request recreates its REQ socket
 * so that callers can safely issue a later request after a timeout.
 */
class Client {
public:
    Client(std::string host = "127.0.0.1", unsigned short port = 5555,
           int timeout_ms = 2000);
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    Reply request(const std::string &command);
    /** Encode and send an IOD command with string/symbol arguments. */
    Reply request_command(const std::string &command,
                          const std::vector<std::string> &arguments = {});
    void reconnect();

private:
    class Impl;
    Impl *impl_;
};

}  // namespace cw_client
