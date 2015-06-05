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

#ifndef cwlang_Command_h
#define cwlang_Command_h
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "value.h"

/*
class IODString {
public:
    IODString(char *s) : s_str(0), str(s) { }
	IODString(const char *s) : s_str(s), str(0) { }
	IODString(const IODString &orig) { 
		if (orig.str) str = strdup(orig.str);
		s_str = orig.s_str;
	}
	IODString & operator=(const IODString &orig) {
		if (orig.str) str = strdup(orig.str); else str = 0;
		s_str = orig.s_str;
		return *this;
	}
    const char *get() const { return (str) ? str : s_str; }
    ~IODString() { if (str) free( str ); }
    const char *s_str;
	char *str;
};
*/

struct IODCommand {
public:
    IODCommand( int minp = 0, int maxp = 100)  : done(false), success(eUnassigned), error_str(""),
		result_str(""), min_params(minp), max_params(maxp) {}
    virtual ~IODCommand(){ }
    bool done;
//    const char *error() { const char *res = error_str.get(); return (res) ? res : "" ;  }
//    const char *result() { const char *res = result_str.get(); return (res) ? res : "" ;  }
    const char *error() { return error_str.c_str(); }
    const char *result() { return result_str.c_str(); }
    
	bool operator()(std::vector<Value> &params) {
		done = run(params);
		return done;
	}
	enum CommandResult { eUnassigned, eFailure, e_Success};
	CommandResult success;
    
protected:
	virtual bool run(std::vector<Value> &params) = 0;
	std::string error_str;
	std::string result_str;
	int min_params;
	int max_params;
};



#endif
