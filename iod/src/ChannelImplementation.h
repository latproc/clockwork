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

#ifndef __cw_CHANNEL_IMPL_H__
#define __cw_CHANNEL_IMPL_H__

#include <ostream>
#include <string>
#include <map>
#include <set>
#include <boost/thread.hpp>
//#include "regular_expressions.h"
//#include "value.h"
//#include "symboltable.h"
//#include "MachineInstance.h"
//#include "MessagingInterface.h"
//#include "SocketMonitor.h"
//#include "IODCommand.h"

class Channel;
class MachineInterface;
class SubscriptionManager;
class MachineRef;

class ChannelImplementation {
public:
	ChannelImplementation();
	virtual ~ChannelImplementation();
	void addMonitor(const char *);
	void addIgnorePattern(const char *);
	void addMonitorPattern(const char *);
	void addMonitorProperty(const char *,const Value &);
	void addMonitorExports();
	void addMonitorLinkedTo(const char *);
	void removeMonitor(const char *);
	void removeIgnorePattern(const char *);
	void removeMonitorPattern(const char *);
	void removeMonitorProperty(const char *, const Value &);
	void removeMonitorExports();

	void processChannelIgnoresList();

	void modified();
	void checked();

	bool monitors() const; // does this channel monitor anything?
	bool updates() const; // does this channel update anything?
	bool shares_machines() const; // does this channel share anything?

	static State DISCONNECTED;
	static State WAITSTART;
	static State CONNECTING;
	static State CONNECTED;
	static State DOWNLOADING;
	static State UPLOADING;
	static State ACTIVE;

	bool monitorsLinked() const { return !monitor_linked.empty(); }
	uint64_t getAuthority() const { return authority; }
	void setAuthority(uint64_t auth) { authority = auth; }

protected:
	std::set<std::string> monitors_patterns;
	std::set<std::string> ignores_patterns;
	std::set<std::string> monitors_names;
	std::set<std::string> monitor_linked;
	std::map<std::string, Value> monitors_properties;
	std::map<std::string, Value> updates_names;
	std::map<std::string, Value> shares_names;
	bool monitors_exports;
	uint64_t last_modified; // if the modified time > check time, a full check will be used
	uint64_t last_checked;
	uint64_t authority; // used to gain permission to change remote objects

private:
	ChannelImplementation(const ChannelImplementation &);
	ChannelImplementation & operator=(const ChannelImplementation &);
};

#endif
