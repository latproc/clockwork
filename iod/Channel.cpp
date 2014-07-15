#include <iostream>
#include "Channel.h"

std::map<std::string, Channel*> *Channel::all = 0;

MachineRef::MachineRef() : refs(1) {
}


Channel::Channel()  {
    if (all == 0) {
        all = new std::map<std::string, Channel*>;
    }
}

Channel::Channel(const Channel &orig){
}

Channel &Channel::operator=(const Channel &other) {
    return *this;
}

std::ostream &Channel::operator<<(std::ostream &out) const  {
    out << "CHANNEL";
    return out;
}

std::ostream &operator<<(std::ostream &out, const Channel &m) {
    return m.operator<<(out);
}

bool Channel::operator==(const Channel &other) {
    return false;
}

ChannelDefinition::ChannelDefinition(const char *n) : MachineClass(n) {
    
}

void ChannelDefinition::setKey(const char *s) {
    psk = s;
}

void ChannelDefinition::setIdent(const char *s) {
    identifier = s;
}
void ChannelDefinition::setVersion(const char *s) {
    version = s;
}
void ChannelDefinition::addShare(const char *s) {
    shares.insert(s);
}
void ChannelDefinition::addMonitor(const char *s) {
    monitors_names.insert(s);
}
void ChannelDefinition::addMonitorPattern(const char *s) {
    monitors_patterns.insert(s);
}
void ChannelDefinition::addUpdates(const char *s) {
    updates_names.insert(s);
}
void ChannelDefinition::addSendName(const char *s) {
    send_messages.insert(s);
}
void ChannelDefinition::addReceiveName(const char *s) {
    recv_messages.insert(s);
}
void ChannelDefinition::addOptionName(const char *n, Value &v) {
    options[n] = v;
}

