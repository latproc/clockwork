//
//  MachineCommandList.h
//  latprocc
//
//  Created by Martin Leadbeater on 6/7/2015.
//

#ifndef latprocc_MachineCommandList_h
#define latprocc_MachineCommandList_h

#include "MachineCommandAction.h"
#include "State.h"
#include <list>
#include <map>
#include <ostream>
#include <string>

class MachineCommandTemplateHolder;
class MachineCommandList {
  public:
    MachineCommandList();
    ~MachineCommandList();

    void add(const char *cmd_name, MachineCommandTemplate *cmd_template,
             State machine_state);

    MachineCommandTemplate *find(const char *seek_name);
    MachineCommandTemplate *find(std::string &seek_name);
    MachineCommandTemplate *find(const char *seek_name,
                                 const char *within_state);
    MachineCommandTemplate *find(std::string &seek_name, State &within_state);

    std::map<std::string, std::list<MachineCommandTemplateHolder *> *>::iterator
    begin();
    std::map<std::string, std::list<MachineCommandTemplateHolder *> *>::iterator
    end();
    std::map<std::string,
             std::list<MachineCommandTemplateHolder *> *>::const_iterator
    begin() const;
    std::map<std::string,
             std::list<MachineCommandTemplateHolder *> *>::const_iterator
    end() const;

  private:
    MachineCommandList(const MachineCommandList &orig);
    MachineCommandList &operator=(const MachineCommandList &other);

    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const MachineCommandList &other);
    std::map<std::string, std::list<MachineCommandTemplateHolder *> *>
        command_templates;
};

class MachineCommandTemplateHolder {
  public:
    MachineCommandTemplateHolder(const char *name, MachineCommandTemplate *,
                                 const State &);
    MachineCommandTemplateHolder(const char *name, MachineCommandTemplate *,
                                 const char *within);
    MachineCommandTemplateHolder(std::string &, MachineCommandTemplate *,
                                 State &within);
    ~MachineCommandTemplateHolder();

    std::string name;
    MachineCommandTemplate *command_template;
    State *state;

  private:
    MachineCommandTemplateHolder(const MachineCommandTemplateHolder &orig);
    MachineCommandTemplateHolder &
    operator=(const MachineCommandTemplateHolder &other);
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const MachineCommandTemplateHolder &other);
};

std::ostream &operator<<(std::ostream &out, const MachineCommandList &m);
std::ostream &operator<<(std::ostream &out,
                         const MachineCommandTemplateHolder &m);

#endif
