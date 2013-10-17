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
#include <string.h>
#include <iostream>
#include "Message.h"
#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>

// Used to generate a unique id for each transmitter.
long Transmitter::next_id;


Message::Message(CStringHolder msg) :text(msg.get()) {
    
}

Message::Message(const Message &orig){
    text = orig.text;
}

Message &Message::operator=(const Message &other) {
    text = other.text;
    return *this;
}

void Receiver::enqueue(const Package &package) { 
	boost::mutex::scoped_lock(q_mutex);
	mail_queue.push_back(package); 
}

std::ostream &Message::operator<<(std::ostream &out) const  {
    out << text;
    return out;
}

std::ostream &operator<<(std::ostream &out, const Message &m) {
    return m.operator<<(out);
}

bool Message::operator==(const Message &other) const {
    return text.compare(other.text) == 0;
}

bool Message::operator==(const char *msg) const {
    if (!msg) return false;
    return strcmp(text.c_str(), msg) == 0;
}

Package::Package(const Package &other) 
	:transmitter(other.transmitter), 
	receiver(other.receiver), 
	message(other.message),
	needs_receipt(other.needs_receipt) {	
}

Package::~Package() {
    delete message;
}

Package &Package::operator=(const Package &other) {
	transmitter = other.transmitter;
	receiver = other.receiver;
	message = new Message(other.message);
	needs_receipt = other.needs_receipt;
	return *this;
}

std::ostream& Package::operator<<(std::ostream &out) const {
    if (receiver)
        return out << "Package: " << *message << " from " << transmitter->getName() << " to " << receiver->getName() 
            << " needs receipt: " << needs_receipt;
    else
        return out << "Package: " << *message << " from " << transmitter->getName() << " needs receipt: " << needs_receipt;
}

std::ostream &operator<<(std::ostream &out, const Package &package) {
	return package.operator<<(out);
}
