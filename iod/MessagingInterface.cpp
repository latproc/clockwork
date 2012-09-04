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
#include <iostream>
#include <exception>
#include <zmq.hpp>

MessagingInterface *MessagingInterface::current = 0;
std::map<std::string, MessagingInterface *>MessagingInterface::interfaces;

MessagingInterface *MessagingInterface::create(std::string host, int port) {
    std::stringstream ss;
    ss << host << ":" << port;
    std::string id = ss.str();
    if (interfaces.count(id) == 0) {
        MessagingInterface *res = new MessagingInterface(host, port);
        interfaces[id] = res;
        return res;
    }
    else
        return interfaces[id];
}

MessagingInterface::MessagingInterface(int num_threads, int port) {
    context = new zmq::context_t(num_threads);
    socket = new zmq::socket_t(*context, ZMQ_PUB);
    is_publisher = true;
    std::stringstream ss;
    ss << "tcp://*:" << port;
    socket->bind(ss.str().c_str());
}

MessagingInterface::MessagingInterface(std::string host, int port) {
    if (host == "*") {
        context = new zmq::context_t(1);
        socket = new zmq::socket_t(*context, ZMQ_PUB);
        is_publisher = true;
        std::stringstream ss;
        ss << "tcp://*:" << port;
        socket->bind(ss.str().c_str());
    }
    else {
        context = new zmq::context_t(1);
        std::stringstream ss;
        ss << "tcp://" << host << ":" << port;
        url = ss.str();
        connect();
    }
}

void MessagingInterface::connect() {
    socket = new zmq::socket_t(*context, ZMQ_REQ);
    is_publisher = false;
    socket->connect(url.c_str());
}

MessagingInterface::~MessagingInterface() {
    if (current == this) current = 0;
    delete socket;
    delete context;
}


void MessagingInterface::send(const char *txt) {
    int len = strlen(txt);
	try {
	    zmq::message_t msg(len);
	    strncpy ((char *) msg.data(), txt, len);
	    socket->send(msg);
        if (!is_publisher) {
            zmq::message_t response;
            
            bool expect_reply = true;
            while (expect_reply) {
                zmq::pollitem_t items[] = { { *socket, 0, ZMQ_POLLIN, 0 } };
                zmq::poll( &items[0], 1, 2000000);
                if (items[0].revents & ZMQ_POLLIN) {
                    zmq::message_t reply;
                    socket->recv(&reply);
                    len = reply.size();
                    char *data = (char *)malloc(len+1);
                    memcpy(data, reply.data(), len);
                    data[len] = 0;
                    std::cout << data << "\n";
                    free(data);
                    return;
                }
                else
                    expect_reply = false;
                    std::cerr << "abandoning message " << txt << "\n";
                    delete socket;
                    connect();
                }
        }
	}
	catch (std::exception e) {
		std::cout << e.what() << "\n";
	}
}
