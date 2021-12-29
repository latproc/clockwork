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

#include <iostream>
#include <string>
#include <zmq.hpp>

int main() {
    //  Prepare our context and socket
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REQ);

    std::cout << "Connecting to io daemon ..." << std::endl;
    socket.connect("tcp://localhost:5555");

    //  Do 10 requests, waiting each time for a response
    for (int request_nbr = 0; request_nbr != 10; request_nbr++) {
        const char *point = "EL2008_OUT_1";
        zmq::message_t request(strlen(point) + 1);
        memcpy((void *)request.data(), point, strlen(point));
        std::cout << "Sending request " << request_nbr << "..." << std::endl;
        socket.send(request);

        //  Get the reply.
        zmq::message_t reply;
        socket.recv(&reply);
        long len = reply.size();
        char *data = new char[len + 1];
        memcpy(data, reply.data(), len);
        data[len] = 0;
        std::cout << "Received reply " << request_nbr << " " << data << std::endl;
        usleep(300000);
    }
    return 0;
}
