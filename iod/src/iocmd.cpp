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

#include "Logger.h"
#include <inttypes.h>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <zmq.hpp>
#define _MAIN_

zmq::context_t *context;

void sendMessage(zmq::socket_t &socket, const char *message) {
    const char *msg = (message) ? message : "";
    size_t len = strlen(msg);
    zmq::message_t reply(len);
    memcpy((void *)reply.data(), msg, len);
    while (true) {
        try {
            socket.send(reply);
            break;
        }
        catch (const zmq::error_t &) {
            if (zmq_errno() == EINTR) {
                continue;
            }
            throw;
        }
    }
}

zmq::socket_t *psocket = 0;

extern void yyparse();
extern FILE *yyin;

void usage(const char *name) { std::cout << name << " [-q] [-h host] [-p port]\n"; }

const char *program_name;

int main(int argc, const char *argv[]) {
    char *pn = strdup(argv[0]);
    program_name = strdup(basename(pn));
    free(pn);

    context = new zmq::context_t;

    try {
        int port = 5555;
        bool quiet = false;
        std::string host = "127.0.0.1";
        for (int i = 1; i < argc; ++i) {
            if (i < argc - 1 && strcmp(argv[i], "-p") == 0) {
                port = (int)strtol(argv[++i], 0, 10);
            }
            else if (i < argc - 1 && strcmp(argv[i], "-h") == 0) {
                host = argv[++i];
            }
            else if (strcmp(argv[i], "-?") == 0) {
                usage(argv[0]);
                exit(0);
            }
            else if (strcmp(argv[i], "-q") == 0) {
                quiet = true;
            }
        }
        std::stringstream ss;
        ss << "tcp://" << host << ":" << port;
        std::string url(ss.str());
        zmq::socket_t socket(*context, ZMQ_REQ);
        int linger = 0; // do not wait at socket close time
        socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        psocket = &socket;

        socket.connect(url.c_str());
        std::string cmd;
        while (std::getline(std::cin, cmd)) {
            sendMessage(*psocket, cmd.c_str());
            if (!quiet) {
                std::cout << cmd << "\n";
            }
            zmq::message_t reply;
            if (psocket->recv(&reply)) {
                auto size = reply.size();
                char *data = (char *)malloc(size + 1);
                memcpy(data, reply.data(), size);
                data[size] = 0;
                std::cout << data << "\n";
            }
        }
    }
    catch (const std::exception &e) {
        if (zmq_errno()) {
            std::cerr << "ZMQ error: " << zmq_strerror(zmq_errno()) << "\n";
        }
        else {
            std::cerr << "Exception raised: " << e.what() << "\n";
        }
        return 1;
    }
    catch (...) {
        std::cerr << "Exception of unknown type!\n";
    }
    return 0;
}
