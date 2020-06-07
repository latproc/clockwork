#include <iostream>
#include <map>
#include <set>
#include <stdlib.h>
#include <string.h>
#include <boost/thread.hpp>
#include "Channel.h"
#include "ChannelImplementation.h"
#include "ChannelDefinition.h"
#include "MessageLog.h"
#include "MessageEncoding.h"
#include "value.h"
#include "State.h"
#include "ChannelImplementation.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "ConnectionManager.h"
#include "Logger.h"
#include "ClientInterface.h"
#include "IODCommand.h"
#include "IODCommands.h"
#include "SyncRemoteStatesAction.h"
#include "DebugExtra.h"
#include "ProcessingThread.h"
#include "SharedWorkSet.h"
#include "MachineInterface.h"
#include "MachineShadowInstance.h"
#include "WaitAction.h"

State ChannelImplementation::CONNECTING("CONNECTING");
State ChannelImplementation::DISCONNECTED("DISCONNECTED");
State ChannelImplementation::DOWNLOADING("DOWNLOADING");
State ChannelImplementation::WAITSTART("WAITSTART");
State ChannelImplementation::UPLOADING("UPLOADING");
State ChannelImplementation::CONNECTED("CONNECTED");
State ChannelImplementation::ACTIVE("ACTIVE");

ChannelImplementation::ChannelImplementation() 
	 : monitors_exports(false), last_modified(0), 
	 	last_checked(0), authority(random()) {
}

ChannelImplementation::~ChannelImplementation() { }

bool ChannelImplementation::monitors() const {
	return monitors_exports
		|| !monitors_names.empty()
		|| !monitors_patterns.empty()
		|| !monitors_properties.empty();
}

bool ChannelImplementation::updates() const {
	return !updates_names.empty();
}

bool ChannelImplementation::shares_machines() const {
	return !shares_names.empty();
}

void ChannelImplementation::addMonitor(const char *s) {
    DBG_CHANNELS << "add monitor for " << s << "\n";
	// if this name had previously been removed there may be a filter for it so we remove that
	std::string pat = "^"; pat += s; pat += "$";
	if (ignores_patterns.count(pat)) {
		DBG_CHANNELS << "removing ignores pattern " << pat << "\n";
		ignores_patterns.erase(pat);
	}
    monitors_names.insert(s);
    modified();
}
void ChannelImplementation::addIgnorePattern(const char *s) {
    DBG_CHANNELS << "adding " << s << " to ignore pattern list\n";
	ignores_patterns.insert(s);
    modified();
}
void ChannelImplementation::removeIgnorePattern(const char *s) {
    DBG_CHANNELS << "remove " << s << " from ignore list\n";
    ignores_patterns.erase(s);
    modified();
}
void ChannelImplementation::removeMonitor(const char *s) {
    DBG_CHANNELS  << s;
	if (monitors_names.count(s)) {
	    monitors_names.erase(s);
    	modified();
		DBG_CHANNELS << "removed monitor for " << s<< "\n";
	}
	else {
		DBG_CHANNELS << "remove monitor for " << s << "...not found adding ignore pattern\n";
		std::string pattern = "^";
		pattern += s;
		pattern += "$";
		addIgnorePattern(pattern.c_str());
		modified();
	}
}
void ChannelImplementation::addMonitorPattern(const char *s) {
	if (ignores_patterns.count(s)) {
		ignores_patterns.erase(s);
		modified();
	}
	if (!monitors_patterns.count(s)) {
		monitors_patterns.insert(s);
		modified();
	}
}
void ChannelImplementation::addMonitorProperty(const char *key, const Value &val) {
    monitors_properties[key] = val;
    modified();
}
void ChannelImplementation::removeMonitorProperty(const char *key, const Value &val) {
    monitors_properties.erase(key);
    modified();
    //TBD Bug here, we should be using a set< pair<string, Value> >, not a map
}
void ChannelImplementation::addMonitorExports() {
	if (!monitors_exports) {
		monitors_exports = true;
		modified();
	}
}
void ChannelImplementation::removeMonitorExports() {
	if (monitors_exports) {
		monitors_exports = false;
		modified();
	}
}

void ChannelImplementation::addMonitorLinkedTo(const char *machine_name) {
	if (monitor_linked.count(machine_name) == 0) {
		monitor_linked.insert(machine_name);
		modified();
	}
}

void ChannelImplementation::removeMonitorPattern(const char *s) {
	if (monitors_patterns.count(s) > 0) {
		monitors_patterns.erase(s);
		modified();
	}
	else {
		DBG_CHANNELS << " adding ignore pattern for " << s << "\n";
		ignores_patterns.insert(s);
		modified();
	}
}

void ChannelImplementation::modified() {
    last_modified = microsecs();
}

void ChannelImplementation::checked() {
    last_checked = microsecs();
}

