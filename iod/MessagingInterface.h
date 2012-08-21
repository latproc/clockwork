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

#ifndef latprocc_MessagingInterface_h
#define latprocc_MessagingInterface_h

#include <zmq.hpp>
#include "Message.h"

class MessagingInterface {
public:
    MessagingInterface(int num_threads, int port) : context(num_threads), publisher(context, ZMQ_PUB) { 
		std::stringstream ss;
		ss << "tcp://*:" << port;
        publisher.bind(ss.str().c_str());
    }
    ~MessagingInterface() { if (current == this) current = 0; }
    void send(const char *msg);
    void setCurrent(MessagingInterface *mi) { current = mi; }
    static MessagingInterface *getCurrent() {
        if (current == 0) current = new MessagingInterface(1, 5556);
        return current; 
    }
private:
    static MessagingInterface *current;
    zmq::context_t context;
    zmq::socket_t publisher;
};

#endif
