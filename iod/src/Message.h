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

#ifndef cwlang_Message_h
#define cwlang_Message_h

#include "symboltable.h"
#include "value.h"
#include <boost/thread/mutex.hpp>
#include <list>
#include <ostream>
#include <set>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include <string>

class CStringHolder {
  public:
    CStringHolder(char *s);
    CStringHolder(const char *s);
    CStringHolder(const CStringHolder &orig);
    CStringHolder &operator=(const CStringHolder &orig);
    ~CStringHolder();
    const char *get() const;
    CStringHolder();

  private:
    const char *s_str;
    char *str;
};

class Message {
  public:
    enum MessageType { SIMPLEMSG, ENTERMSG, LEAVEMSG, ENABLEMSG, DISABLEMSG };
    typedef std::list<Value> Parameters;

    Message(MessageType t = SIMPLEMSG) : kind(t), seq(++sequence), text(""), params(0) {}
    Message(CStringHolder msg, MessageType t = SIMPLEMSG, Parameters *p = 0);
    Message(const Message &orig);
    Message &operator=(const Message &other);
    ~Message();
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const Message &other) const;
    bool operator==(const char *msg) const;
    bool operator!=(const Message &other) const { return !(*this == other); }
    bool operator!=(const char *msg) const { return !(*this == msg); }
    bool operator<(const Message &other) const { return text < other.text; }
    bool operator>(const Message &other) const { return text > other.text; }
    const std::string getText() const { return text; }
    const std::list<Value> *getParams() const { return params; }

    MessageType getType() const { return kind; }
    bool isEnter() const { return kind == ENTERMSG; }
    bool isLeave() const { return kind == LEAVEMSG; }
    bool isSimple() const { return kind == SIMPLEMSG; }
    bool isEnable() const { return kind == ENABLEMSG; }
    bool isDisable() const { return kind == DISABLEMSG; }

    static std::list<Value> *makeParams(Value p1, Value p2 = SymbolTable::Null,
                                        Value p3 = SymbolTable::Null, Value p4 = SymbolTable::Null);

  private:
    static unsigned long sequence;
    MessageType kind;
    unsigned long seq;
    std::string text;
    std::list<Value> *params;
};

std::ostream &operator<<(std::ostream &out, const Message &m);

class Receiver;
class Transmitter {
  public:
    Transmitter(CStringHolder name_str)
        : id(++next_id), _name(name_str.get()), allow_debug(false) {}
    virtual ~Transmitter();
    Transmitter() : id(++next_id) {
        std::stringstream ss;
        ss << id;
        _name = ss.str();
    }
    virtual void sendMessageToReceiver(const Message &m, Receiver *r = NULL, bool expect_reply = false);
    virtual void sendMessageToReceiver(const char *msg, Receiver *r = NULL, bool expect_reply = false);
    virtual const std::string &getName() const { return _name; }
    virtual Receiver *asReceiver() { return 0; }
    virtual bool debug() { return allow_debug; }
    virtual bool enabled() const { return true; }

  protected:
    long id;
    static long next_id;
    std::string _name;
    bool allow_debug;
};

struct Package {
    Transmitter *transmitter;
    Receiver *receiver;
    Message *message;
    bool needs_receipt;
    Package(Transmitter *t, Receiver *r, const Message &m, bool need_receipt = false)
        : transmitter(t), receiver(r), message(new Message(m)), needs_receipt(need_receipt) {}
    Package(const Package &);
    ~Package();
    Package &operator=(const Package &);
    std::ostream &operator<<(std::ostream &out) const;
};
std::ostream &operator<<(std::ostream &out, const Package &package);


#endif
