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

#ifndef __cw_CHANNEL_DEFN_H__
#define __cw_CHANNEL_DEFN_H__

#include <ostream>
#include <string>
#include <map>
#include <set>
#include <boost/thread.hpp>
#include "regular_expressions.h"
#include "value.h"
#include "symboltable.h"
#include "MachineInstance.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "IODCommand.h"
#include "ChannelImplementation.h"

class Channel;
class MachineInterface;
class SubscriptionManager;
class MachineRef;

class ChannelDefinition : public MachineClass, public ChannelImplementation {
public:
    enum Feature { ReportStateChanges, ReportPropertyChanges, ReportModbusUpdates, ReportLocalPropertyChanges };

    void addFeature(Feature f);
    void removeFeature(Feature f);
    bool hasFeature(Feature f) const;

    ChannelDefinition(const char *name, ChannelDefinition *parent = 0);
    Channel *instantiate(unsigned int port);
    static void instantiateInterfaces();
    static ChannelDefinition *find(const char *name);
    static ChannelDefinition *fromJSON(const char *json);
    char *toJSON();
    bool isPublisher() const; // channel can be shared by multiple subscribers
    void setPublisher(bool which);
    
    void setKey(const char *);
    void setIdent(const char *);
    void setVersion(const char *);
    void addShare(const char *nm, const char *if_nm);
    void addUpdates(const char *name, const char *interface_name);
    void addShares(const char *name, const char *interface_name);
    void addSendName(const char *);
    void addReceiveName(const char *);
    void addOptionName(const char *n, Value &v);

    void processIgnoresPatternList(std::set<std::string>::const_iterator first, std::set<std::string>::const_iterator last, Channel *chn) const;

    MachineClass* interfaceFor(const char *monitored_machine) const;

    void setThrottleTime(unsigned int t) { throttle_time = t; }
    unsigned int getThrottleTime() { return throttle_time; }

    const std::string &getIdent() { return identifier; }
    const std::string &getKey() { return psk; }
    const std::string &getVersion() { return version; }

    Value getValue(const char *property);

private:
    static std::map< std::string, ChannelDefinition* > *all;
    std::map<std::string, MachineRef *> interfaces;
    std::set<Feature> features;
    ChannelDefinition *parent;
    bool ignore_response;
    std::string psk; // preshared key
    std::string identifier;
    std::string version;
    std::set<std::string> send_messages;
    std::set<std::string> recv_messages;
    std::map<std::string, Value> options;
    unsigned int throttle_time;
    
    friend class Channel;
};

class MachineRecord {
public:
    MachineInstance *machine;
    uint64_t last_sent;
    std::map<std::string, Value> properties;
    MachineRecord(MachineInstance *m);
private:
    MachineRecord(const MachineRecord &other);
    MachineRecord &operator=(const MachineRecord &other);
};

#endif
