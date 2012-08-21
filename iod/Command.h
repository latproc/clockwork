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

#ifndef latprocc_Command_h
#define latprocc_Command_h

class String {
public:
    String(char *s) : s_str(0), str(s) { }
	String(const char *s) : s_str(s), str(0) { }
	String(const String &orig) { 
		if (orig.str) str = strdup(orig.str);
		s_str = orig.s_str;
	}
	String & operator=(const String &orig) {
		if (orig.str) str = strdup(orig.str);
		s_str = orig.s_str;
		return *this;
	}
    const char *get() const { return (str) ? str : s_str; }
    ~String() { if (str) free( str ); }
    const char *s_str;
	char *str;
};

struct Command {
public:
    Command()  : done(false), error_str(""), result_str("") {}
    virtual ~Command(){ }
    bool done;
    const char *error() { return (error_str.str) ? error_str.get() : "" ;  }
    const char *result() { return (result_str.str) ? result_str.get() : "" ;  }
    
	bool operator()(std::vector<std::string> &params) {
		done = run(params);
		return done;
	}
    
protected:
	virtual bool run(std::vector<std::string> &params) = 0;
	String error_str;
	String result_str;
};



#endif
