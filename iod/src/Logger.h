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

#include <ostream>
#include <libgen.h>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/time.h>
#include <vector>

extern const char *program_name;
class FileLogger {
  public:
    std::ostream &f();
    FileLogger(const char *fname);
    ~FileLogger();

    void getTimeString(char *buf, size_t buf_size);
private:
    class Internals;
    Internals *internals;
};

class LogState {
  public:
    static LogState *instance();
    static void cleanup();
    int define(std::string new_name);
    int lookup(const std::string &name);
    int insert(int flag_num);
    int insert(std::string name);
    void erase(int flag_num);
    void erase(std::string name);
    bool includes(int flag_num);
    bool includes(std::string name);
    std::ostream &operator<<(std::ostream &out) const;

    typedef std::map<std::string, int>::iterator NameMapIterator;

  private:
    LogState() {}
    static LogState *state_instance;
    std::set<int> state_flags;
    std::vector<std::string> flag_names;
    std::map<std::string, int> name_map;
};

std::ostream &operator<<(std::ostream &, const LogState &);

class Logger {
  public:
    static Logger *instance();

    typedef int Level;
    static int None;
    static int Important;
    static int Debug;
    static int Everything;
    Level level();
    void setLevel(Level n);
    void setLevel(std::string level_name);
    std::ostream &log(Level l);
    void setOutputStream(std::ostream *out);
    static void getTimeString(char *buf, size_t buf_size);
    static void cleanup();

    class Internals;
    static Internals *internals;

  private:
    ~Logger();
    Logger();
};

#define LOGS(l) (LogState::instance()->includes((l)))
#define MSG(l)                                                                                     \
    if (!LogState::instance()->includes((l)))                                                      \
        ;                                                                                          \
    else                                                                                           \
        Logger::instance()->log((l))
#define M_MSG(l, m)                                                                                \
    if (!(m->debug() && LogState::instance()->includes((l))))                                      \
        ;                                                                                          \
    else                                                                                           \
        Logger::instance()->log((l))
#define DBG_MSG MSG(Logger::Debug)
#define DBG_M_MSG M_MSG(Logger::Debug, this)
#define NB_MSG MSG(Logger::Important)
#define INF_MSG MSG(Logger::Everything)

#endif
