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
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <zmq.hpp>
#include "MessageEncoding.h"
#include "value.h"
#include <list>
#define _MAIN_

zmq::context_t *context = nullptr;
zmq::socket_t *psocket = nullptr;
std::string line_input;

std::list<Value> params;
bool quiet = true;
char eol_char = '\n'; // Default end-of-line character
bool cmdline_done = false;

extern void yyparse();
extern FILE *yyin;

bool sendMessage(zmq::socket_t &socket, const char *message) {
    const char *msg = (message) ? message : "";
    size_t len = strlen(msg);
    if (len == 0) { return false; }
    std::cout << "Sending: " << msg << "\n";
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
    return true;
}

char *send_command(std::list<Value> &params) {
    if (params.size() == 0) {
        return 0;
    }
    Value cmd_val = params.front();
    params.pop_front();
    std::string cmd = cmd_val.asString();
    std::string msg = MessageEncoding::encodeCommand(cmd, params);
    sendMessage(*psocket, msg.c_str());
    size_t size;
    zmq::message_t reply;
    if (psocket->recv(&reply)) {
        size = reply.size();
        char *data = (char *)malloc(size + 1);
        if (data) {
            memcpy(data, reply.data(), size);
            data[size] = 0;
        }
        return data;
    }
    return nullptr;
}
void process_command(std::list<Value> &params) {
    char *data = send_command(params);
    if (data) {
        std::cout << data << "\n";
        free(data);
    }
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return ""; // String is all whitespace
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Read a line from input stream until the specified delimiter character is found
bool getLineWithDelimiter(std::istream& input, std::string& cmd, char delimiter) {
    cmd.clear();
    int ch;
    while (true) {
        std::string cmd;
        while ((ch = input.get()) != EOF) {
            if (ch == delimiter) {
                break;
            }
            if (ch == '\r' || ch == '\n' || ch == '\t') {
                ch = ' ';
            }
            cmd += static_cast<char>(ch);
        }
        if (ch == EOF) {
            return false;
        }
        if (cmd.compare(0, 4, "eol=") == 0 && cmd.length() >= 5) {
            delimiter = cmd[4];
            if (!quiet) {
                std::cerr << "EOL character changed to: ";
                if (isprint(delimiter)) {
                    std::cerr << "'" << delimiter << "'";
                }
                else {
                    std::cerr << std::oct << delimiter << std::dec << " (" << (int)delimiter << ")";
                }
                std::cerr << "\n";
            }
            continue;
        }
        if (cmd.empty()) { continue; }
        if (!quiet) {
            std::cout << cmd << "\n";
        }

        line_input = trim(cmd);
        return true;
    }
}

std::string get_input_line() {
    std::string line;
    if (!getLineWithDelimiter(std::cin, line, eol_char)) {
        cmdline_done = true;
        return "";
    }
    line_input += ";";
    return line_input;
}

void usage(const char *name) { 
    std::cout << name << " [-v] [-h host] [-p port] [--eol=<char>]\n"
              << "  -v         : verbose mode\n"
              << "  -h host    : host to connect to (default: 127.0.0.1)\n"
              << "  -p port    : port to connect to (default: 5555)\n"
              << "  --eol=<c>  : end-of-line character (default: newline)\n"
              << "               Use 'eol=<c>' in stdin to change dynamically\n";
}

const char *program_name;

int main(int argc, const char *argv[]) {
    char *pn = strdup(argv[0]);
    program_name = strdup(basename(pn));
    free(pn);

    context = new zmq::context_t;

    try {
        int port = 5555;
        std::string host = "127.0.0.1";

        for (int i = 1; i < argc; ++i) {
            if (i < argc - 1 && strcmp(argv[i], "-p") == 0) {
                port = (int)strtol(argv[++i], 0, 10);
            }
            else if (i < argc - 1 && strcmp(argv[i], "-h") == 0) {
                host = argv[++i];
            }
            else if (strncmp(argv[i], "--eol=", 6) == 0) {
                const char *eol_str = argv[i] + 6;
                if (strlen(eol_str) > 0) {
                    eol_char = eol_str[0];
                }
            }
            else if (strcmp(argv[i], "-?") == 0) {
                usage(argv[0]);
                exit(0);
            }
            else if (strcmp(argv[i], "-v") == 0) {
                quiet = false;
            }
        }
        std::stringstream ss;
        ss << "tcp://" << host << ":" << port;
        std::string url(ss.str());
        psocket = new zmq::socket_t(*context, ZMQ_REQ);
        int linger = 0; // do not wait at socket close time
        psocket->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

        psocket->connect(url.c_str());
        yyparse();
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
