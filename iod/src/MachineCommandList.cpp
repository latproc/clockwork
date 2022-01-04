//
//  MachineCommandList.cpp
//  latprocc
//
//  Created by Martin Leadbeater on 19/03/12.
//  Copyright (c) 2012 Tellurian Pty Ltd. All rights reserved.
//

#include "MachineCommandList.h"
#include <iostream>

MachineCommandTemplateHolder::MachineCommandTemplateHolder(const char *name_,
                                                           MachineCommandTemplate *mct,
                                                           const char *within)
    : name(name_), command_template(mct), state(0) {
    if (within) {
        state = new State(within);
    }
}
MachineCommandTemplateHolder::MachineCommandTemplateHolder(std::string &name_,
                                                           MachineCommandTemplate *mct,
                                                           State &within)
    : name(name_), command_template(mct), state(0) {
    state = new State(within);
}

MachineCommandTemplateHolder::~MachineCommandTemplateHolder() {
    if (state) {
        delete state;
    }
}

MachineCommandTemplateHolder::MachineCommandTemplateHolder(const char *name,
                                                           MachineCommandTemplate *,
                                                           const State &) {}

MachineCommandList::MachineCommandList() {}

void MachineCommandList::add(const char *cmd_name, MachineCommandTemplate *cmd_template,
                             State machine_state) {

    MachineCommandTemplateHolder *mcth =
        new MachineCommandTemplateHolder(cmd_name, cmd_template, machine_state);

    std::map<std::string, std::list<MachineCommandTemplateHolder *> *>::iterator found =
        command_templates.find(mcth->name);
    std::list<MachineCommandTemplateHolder *> *cmds = 0;
    if (found == command_templates.end()) {
        cmds = new std::list<MachineCommandTemplateHolder *>();
        command_templates[mcth->name] = cmds;
    }
    else {
        cmds = (*found).second;
    }
    cmds->push_back(mcth);
}

// find the general handler for the given message name
MachineCommandTemplate *MachineCommandList::find(const char *seek_name) {
    std::map<std::string, std::list<MachineCommandTemplateHolder *> *>::iterator found =
        command_templates.find(seek_name);
    if (found == command_templates.end()) {
        return 0;
    }

    std::list<MachineCommandTemplateHolder *> *cmds = (*found).second;
    std::list<MachineCommandTemplateHolder *>::const_iterator iter = cmds->begin();
    while (iter != cmds->end()) {
        MachineCommandTemplateHolder *mcth = *iter++;
        if (mcth->state == 0) {
            return mcth->command_template;
        }
    }
    return 0;
}

MachineCommandTemplate *MachineCommandList::find(std::string &seek_name) {
    return find(seek_name.c_str());
}

MachineCommandTemplate *MachineCommandList::find(std::string &seek_name, State &within_state) {
    std::map<std::string, std::list<MachineCommandTemplateHolder *> *>::iterator found =
        command_templates.find(seek_name);
    if (found == command_templates.end()) {
        return 0;
    }

    std::list<MachineCommandTemplateHolder *> *cmds = (*found).second;
    std::list<MachineCommandTemplateHolder *>::const_iterator iter = cmds->begin();
    while (iter != cmds->end()) {
        MachineCommandTemplateHolder *mcth = *iter++;
        if (mcth->state && *mcth->state == within_state) {
            return mcth->command_template;
        }
    }
    return 0;
}

MachineCommandTemplate *MachineCommandList::find(const char *seek_name, const char *within_state) {
    std::string nam(seek_name);
    State s(within_state);
    return find(nam, s);
}

std::map<std::string, std::list<MachineCommandTemplateHolder *> *>::iterator
MachineCommandList::begin() {
    return command_templates.begin();
}

std::map<std::string, std::list<MachineCommandTemplateHolder *> *>::iterator
MachineCommandList::end() {
    return command_templates.end();
}

std::map<std::string, std::list<MachineCommandTemplateHolder *> *>::const_iterator
MachineCommandList::begin() const {
    return command_templates.begin();
}

std::map<std::string, std::list<MachineCommandTemplateHolder *> *>::const_iterator
MachineCommandList::end() const {
    return command_templates.end();
}

#if 0
MachineCommandList::MachineCommandList(const MachineCommandList &orig)
{
}

MachineCommandList &MachineCommandList::operator=(const MachineCommandList &other)
{
    text = other.text;
    return *this;
}

std::ostream &MachineCommandList::operator<<(std::ostream &out) const
{
    out << text;
    return out;
}

std::ostream &operator<<(std::ostream &out, const MachineCommandList &m)
{
    return m.operator << (out);
}

bool MachineCommandList::operator==(const MachineCommandList &other)
{
    return text == other.text;
}
#endif
