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

#ifndef cwlang_State_h
#define cwlang_State_h

#include <ostream>
#include <string>
#include <lib_clockwork_client/includes.hpp>
// // #include "value.h"

class StateBase {
public:
	StateBase(bool priv = false, bool trans = false);
	virtual ~StateBase() {}

	bool isPrivate() const;
	void setPrivate(bool which);

	bool isTransitional() const;
	void setTransitional(bool which);

protected:
	bool is_private;
	bool is_transitional; // is a leaving or entering transitional state

};

class State : public StateBase {
public:
	State(const std::string &name);
	State(const char *name);
	State(int val);
	State(const State &orig);
	virtual ~State() {}
	State &operator=(const State &other);
	std::ostream &operator<<(std::ostream &out) const;
	virtual bool operator==(const State &other) const;
	virtual bool operator!=(const State &other) const;
	const std::string &getName() const { return text; }
	int getId() const { return token_id; }
	bool is(int tok) { return token_id == tok; }
	const Value *getNameValue() const { return &name; }
	int getIntValue() { return val; }

	void enter(void *data) const;
	void setEnterFunction( void (*f)(void *) );

private:
	std::string text;
	int val;
	Value name;
	int token_id;
	void (*enter_proc)(void *);
};

std::ostream &operator<<(std::ostream &out, const State &m);



#endif
