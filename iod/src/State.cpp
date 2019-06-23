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

#include <string.h>
#include <iostream>
#include "State.h"
#include "symboltable.h"


bool StateBase::isPrivate() const { return is_private; }
void StateBase::setPrivate(bool which) { is_private = which; }

bool StateBase::isTransitional() const { return is_transitional; }
void StateBase::setTransitional(bool which) { is_transitional = which; }

State::State(const std::string &msg) :text(msg), val(0), name(msg, Value::t_string),
		enter_proc(0)  {
    token_id = Tokeniser::instance()->getTokenId(text.c_str());
}

State::State(const char *msg) :text(msg), val(0), name(msg, Value::t_string),
enter_proc(0) {
	token_id = Tokeniser::instance()->getTokenId(text.c_str());
}


State::State(const State &orig){
	text = orig.text;
	val = orig.val;
	name = Value(orig.name);
	token_id = Tokeniser::instance()->getTokenId(text.c_str());
	is_private = orig.is_private;
	is_transitional = orig.is_transitional;
}

State &State::operator=(const State &other) {
	text = other.text;
	val = other.val;
	name = other.name;
	token_id = Tokeniser::instance()->getTokenId(text.c_str());
	is_private = other.is_private;
	is_transitional = other.is_transitional;
	return *this;
}

// strictly a state cannot output its name  if it is private,
// instead, the previous state of the owning machine should be
// displayed.
std::ostream &State::operator<<(std::ostream &out) const  {
	if (is_private)
		out << "|" << text << ((is_transitional)?"*":"") << "|";
	else
    out << text << ((is_transitional)?"*":"");

    return out;
}

std::ostream &operator<<(std::ostream &out, const State &m) {
    return m.operator<<(out);
}

bool State::operator==(const State &other) const {
    return token_id == other.token_id && val == other.val
			&& is_private == other.is_private && is_transitional == other.is_transitional;
}

bool State::operator!=(const State &other) const {
    return token_id != other.token_id || val != other.val
			|| is_transitional != other.is_transitional || is_private != other.is_private;
}

void State::enter(void *data) const {
	if (enter_proc) {
		enter_proc(data);
	}

}

void State::setEnterFunction( void (*f)(void *) ) {
	enter_proc = f;
}



