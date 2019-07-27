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

#ifndef __ClientInterface_h__
#define __ClientInterface_h__

class IODCommand;

struct ClientInterfaceInternals { virtual ~ClientInterfaceInternals(); };
    
struct IODCommandListenerThread {
    void operator()();
    IODCommandListenerThread();
    void stop();
    bool done;
//private:
//    ClientInterfaceInternals *internals;
};

IODCommand *parseCommandString(const char *data);

class IODCommand;
class IODCommandFactory;
class IODCommandThread {
public:
	static IODCommandThread *instance();
    void operator()();
    void stop();
    bool done;
	static void registerCommand(std::string name, IODCommandFactory *cmd);
/*
	void newPendingCommand(IODCommand *cmd);
	IODCommand *getCommand();
	void putCompletedCommand(IODCommand *cmd);
	IODCommand *getCompletedCommand();
*/
protected:
	ClientInterfaceInternals *internals;
	friend IODCommand *parseCommandString(const char *data);
private:
	IODCommandThread();
	~IODCommandThread();

	static IODCommandThread *instance_;

	IODCommandThread(const IODCommandThread &);
	IODCommandThread &operator=(const IODCommandThread &);
};

#endif
