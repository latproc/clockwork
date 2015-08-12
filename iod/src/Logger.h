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

#ifndef cwlang_Logger_h
#define cwlang_Logger_h

#include <set>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include "boost/thread/mutex.hpp"
#include <fstream>
#include <libgen.h>
#include <sys/time.h>

extern const char *program_name;
class FileLogger {
public:
  std::ofstream f;
  FileLogger(const char *fname);

	void getTimeString(char *buf, size_t buf_size);
};


class LogState {
public:
	static LogState* instance() { if (!state_instance) state_instance = new LogState; return state_instance; }
	int define(std::string new_name) {
		flag_names.push_back(new_name);
		name_map[new_name] = (int)flag_names.size()-1;
		return (int)flag_names.size()-1;
	}
	int lookup(const std::string &name) { if (name_map.count(name)) return name_map[name]; else return 0; }
	int insert(int flag_num) { state_flags.insert(flag_num); return flag_num; }
	int insert(std::string name) { 
		if (name_map.count(name)) { 
			int n = name_map[name]; state_flags.insert(n); return n; 
		}
		return -1;
	}
	void erase(int flag_num) { state_flags.erase(flag_num); }
	void erase(std::string name) { if (name_map.count(name)) state_flags.insert(name_map[name]); }
	bool includes(int flag_num) { return state_flags.count(flag_num); }
	bool includes(std::string name) { if (name_map.count(name)) return state_flags.count(name_map[name]); else return false; }
	std::ostream &operator<<(std::ostream &out) const;

private:
	LogState(){}
	static LogState *state_instance;
	std::set<int>state_flags;
	std::vector<std::string>flag_names;
	std::map<std::string, int>name_map;
};

std::ostream &operator<<(std::ostream &, const LogState&);

class Logger{
public:
	static Logger* instance() { if (!logger_instance) logger_instance = new Logger; return logger_instance; }
	typedef int Level;
    static int None;
	static int Important;
	static int Debug;
	static int Everything;
    Level level(){return log_level;}
    void setLevel(Level n){log_level=n;}
    void setLevel(std::string level_name);
    std::ostream&log(Level l);
    void setOutputStream(std::ostream *out) { log_stream = out; }
	static void getTimeString(char *buf, size_t buf_size);
private:
	Logger();
	static Logger *logger_instance;
    Level log_level;
    std::stringstream*dummy_output;
    Logger(const Logger&);
    Logger&operator=(const Logger&);
    std::ostream *log_stream;
    boost::mutex mutex_;
};

#define LOGS(l) ( LogState::instance()->includes( (l) ))
#define MSG(l) 	if (!LogState::instance()->includes( (l) )) ; else Logger::instance()->log( (l) )
#define M_MSG(l,m) 	if ( !(m->debug() && LogState::instance()->includes( (l) )) ) ; else Logger::instance()->log( (l) )
#define DBG_MSG MSG(Logger::Debug)
#define DBG_M_MSG M_MSG(Logger::Debug,this)
#define NB_MSG  MSG(Logger::Important)
#define INF_MSG MSG(Logger::Everything)

#endif
