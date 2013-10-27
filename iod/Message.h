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

#include <ostream>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include <list>
#include <boost/thread/mutex.hpp>

class CStringHolder {
public:
    CStringHolder(char *s) : s_str(0), str(s) { }
	CStringHolder(const char *s) : s_str(0), str(strdup(s)) { }
	CStringHolder(const CStringHolder &orig)  : s_str(0), str(0) {
		if (orig.str) str = strdup(orig.str);
		s_str = orig.s_str;
	}
	CStringHolder & operator=(const CStringHolder &orig) {
        if (str) { free(str); str = 0; }
		if (orig.str) str = strdup(orig.str);
		s_str = orig.s_str;
		return *this;
	}
    ~CStringHolder() { if (str) free(str); }
    const char *get() const { return (str) ? str : s_str; }
    CStringHolder() { if (str) free( str ); }
    const char *s_str;
	char *str;
};

class Message {
public:
    Message() {}
    Message(Message *m) : text(m->text) {}
    Message(CStringHolder msg);
    Message(const Message &orig);
    Message &operator=(const Message &other);
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const Message &other) const;
    bool operator==(const char *msg) const;
    bool operator<(const Message &other) const { return text < other.text; }
    bool operator>(const Message &other) const { return text > other.text; }
    const std::string getText() const { return text; }
    
private:
    std::string text;
};

std::ostream &operator<<(std::ostream &out, const Message &m);

class Receiver;
class Transmitter {
public:
	Transmitter(CStringHolder name_str) : id(++next_id), _name(name_str.get()), allow_debug(false) {	}
    void send(Message *m, Receiver *r = NULL, bool expect_reply = false);
    Transmitter() : id(++next_id) { 
        std::stringstream ss;
        ss << id;
        _name = ss.str();
    }
	virtual ~Transmitter() { }
    const std::string &getName() const { return _name; }
	virtual Receiver *asReceiver() { return 0; }
	virtual bool debug() { return allow_debug; }
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
    Package(Transmitter *t, Receiver *r, Message *m, bool need_receipt = false) 
		: transmitter(t), receiver(r), message(m), needs_receipt(need_receipt) {}
    Package(Transmitter *t, Receiver *r, const Message &m, bool need_receipt = false) 
		: transmitter(t), receiver(r), message(new Message(m)), needs_receipt(need_receipt) {}
	Package(const Package &);
    ~Package();
	Package &operator=(const Package &);
	std::ostream &operator<<(std::ostream &out) const;
};
std::ostream &operator<<(std::ostream &out, const Package &package);

class Receiver : public Transmitter {
public:
	Receiver(CStringHolder name_str) : Transmitter(name_str) {}
    virtual bool receives(const Message&, Transmitter *t) = 0;
    virtual void handle(const Message&, Transmitter *from, bool needs_receipt = false ) = 0;
	virtual void enqueue(const Package &package);
    long getId() const { return id; }
protected:
		
std::list<Package> mail_queue;
static boost::mutex q_mutex;

};


#endif
