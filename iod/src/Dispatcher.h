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

#ifndef cwlang_Dispatcher_h
#define cwlang_Dispatcher_h

#include "zmq.hpp"
#include <boost/thread.hpp>
#include <list>
#include <map>
#include <ostream>
#include <string>
#include <utility>

class Message;
class Receiver;
struct Package;

class DispatchThread {
  public:
    void operator()();
};

class Dispatcher {
    Dispatcher();
  public:
    Dispatcher(const Dispatcher &orig) = delete;
    Dispatcher &operator=(const Dispatcher &other) = delete;

    std::ostream &operator<<(std::ostream &out) const;
    void deliver(Package *p);
    void deliverZ(Package *p);
    void addReceiver(Receiver *r);
    void removeReceiver(Receiver *r);
    static Dispatcher *instance();
    static void start();
    void idle();
    void stop();

  private:
};

std::ostream &operator<<(std::ostream &out, const Dispatcher &m);

#endif
