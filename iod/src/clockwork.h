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

#ifndef _CLOCKWORK_H__
#define _CLOCKWORK_H__

#include <list>
#include <string>
#include <value.h>

extern std::list<std::string> error_messages;
extern int num_errors;

class MachineInstance;

int loadOptions(int argc, char const *argv[], std::list<std::string> &files);
int loadConfig(std::list<std::string> &files);

void initialise_machines();

class ClockworkProcessManager {
  public:
    void SetTime(uint64_t t);
};

class ClockworkInterpreter {
  public:
    static ClockworkInterpreter *instance();
    void setup(MachineInstance *new_settings);
    MachineInstance *settings();

    const Value *cycle_delay;
    Value *default_poll_delay;

    inline uint64_t now() const { return current_time; }

  private:
    ClockworkInterpreter();
    ClockworkInterpreter(const ClockworkInterpreter &);
    ClockworkInterpreter &operator=(const ClockworkInterpreter &);
    static ClockworkInterpreter *_instance;

    MachineInstance *_settings;

  protected:
    uint64_t current_time;

    friend class ClockworkProcessManager;
};

#endif
