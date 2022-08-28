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

#include "Message.h"
#include "value.h"
#include <algorithm>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <iostream>
#include <list>
#include <set>
#include <string.h>
#include "Receiver.h"

// Used to generate a unique id for each transmitter.
long Transmitter::next_id;
boost::mutex Receiver::q_mutex;
unsigned long Message::sequence = 0; // each message has a sequence number

CStringHolder::CStringHolder(char *s) : s_str(0), str(s) {}

CStringHolder::CStringHolder(const char *s) : s_str(0), str(strdup(s)) {}

CStringHolder::CStringHolder(const CStringHolder &orig) : s_str(0), str(0) {
    if (orig.str) {
        str = strdup(orig.str);
    }
    s_str = orig.s_str;
}

CStringHolder &CStringHolder::operator=(const CStringHolder &orig) {
    if (str) {
        free(str);
        str = 0;
    }
    if (orig.str) {
        str = strdup(orig.str);
    }
    s_str = orig.s_str;
    return *this;
}
CStringHolder::~CStringHolder() {
    if (str) {
        free(str);
    }
}

const char *CStringHolder::get() const { return (str) ? str : s_str; }

//CStringHolder::CStringHolder() { if (str) free( str ); }

std::set<Receiver *> Receiver::working_machines; // machines that have non-empty work queues

// in this form, the message takes ownership of the parameters
Message::Message(CStringHolder msg, MessageType t, std::list<Value> *param_list)
    : kind(t), seq(++sequence), text(msg.get()), params(0) {
    params = param_list;
}

// in this form, the message takes ownership of the parameters
Message::Message(Message *m, MessageType t, Parameters *p)
    : kind(t), seq(++sequence), text(m->text), params(p) {}

Message::Message(const Message &orig) : kind(orig.kind), seq(orig.seq), text(orig.text), params(0) {
    if (orig.params) {
        params = new std::list<Value>();
        std::copy(orig.params->begin(), orig.params->end(), back_inserter(*params));
    }
}

Message &Message::operator=(const Message &other) {
    kind = other.kind;
    seq = other.seq;
    text = other.text;
    if (other.params) {
        params = new std::list<Value>();
        std::copy(other.params->begin(), other.params->end(), back_inserter(*params));
    }
    else {
        params = 0;
    }
    return *this;
}

Message::~Message() {
    if (params) {
        params->clear();
        delete params;
        params = 0;
    }
}

std::list<Value> *Message::makeParams(Value p1, Value p2, Value p3, Value p4) {
    std::list<Value> *params = new std::list<Value>();
    params->push_back(p1);
    if (p2 != SymbolTable::Null) {
        params->push_back(p2);
    }
    else {
        return params;
    }
    if (p3 != SymbolTable::Null) {
        params->push_back(p3);
    }
    else {
        return params;
    }
    if (p4 != SymbolTable::Null) {
        params->push_back(p4);
    }
    return params;
}

void Receiver::enqueue(const Package &package) {
    boost::mutex::scoped_lock lock(q_mutex);
    mail_queue.push_back(package);
    has_work = true;
}

std::ostream &Message::operator<<(std::ostream &out) const {
    out << text << " " << kind << " ";
    return out;
}

std::ostream &operator<<(std::ostream &out, const Message &m) { return m.operator<<(out); }

bool Message::operator==(const Message &other) const { return text.compare(other.text) == 0; }

bool Message::operator==(const char *msg) const {
    if (!msg) {
        return false;
    }
    return strcmp(text.c_str(), msg) == 0;
}

Package::Package(const Package &other)
    : transmitter(other.transmitter), receiver(other.receiver), message(new Message(other.message)),
      needs_receipt(other.needs_receipt) {}

Package::~Package() { delete message; }

Package &Package::operator=(const Package &other) {
    transmitter = other.transmitter;
    receiver = other.receiver;
    message = new Message(other.message);
    needs_receipt = other.needs_receipt;
    return *this;
}

std::ostream &Package::operator<<(std::ostream &out) const {
    if (receiver)
        return out << "Package: " << *message << " from " << transmitter->getName() << " to "
                   << receiver->getName() << " needs receipt: " << needs_receipt;
    else {
        return out << "Package: " << *message << " from " << transmitter->getName()
                   << " needs receipt: " << needs_receipt;
    }
}

std::ostream &operator<<(std::ostream &out, const Package &package) {
    return package.operator<<(out);
}

Transmitter::~Transmitter() {}

void Transmitter::sendMessageToReceiver(Message *m, Receiver *r, bool expect_reply) {
    assert(false);
}

bool Receiver::hasPending(const Message &msg) {
    boost::mutex::scoped_lock lock(q_mutex);
    std::list<Package>::iterator iter = mail_queue.begin();
    while (iter != mail_queue.end()) {
        const Package &p = *iter++;
        if (p.message && *p.message == msg) {
            return true;
        }
    }
    return false;
}
