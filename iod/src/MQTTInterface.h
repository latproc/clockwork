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

#ifndef __MQTTInterface
#define __MQTTInterface

#include "Message.h"
#include "symboltable.h"
#include <iostream>
#include <mosquitto.h>
#include <sys/types.h>

class MachineInstance;

class MQTTModule : public Transmitter {
  public:
    enum Status { STATUS_CONNECTING, STATUS_CONNACK_RECVD, STATUS_WAITING, STATUS_ERROR };

    MQTTModule(const char *name);
    bool online();
    std::ostream &operator<<(std::ostream &) const;

  public:
    Status status;
    struct mosquitto *mosq;
    std::string host;
    int port;
    SymbolTable pubs;
    SymbolTable subs;
    std::map<std::string, MachineInstance *> handlers;

    int last_error;
    std::string last_error_str;

    bool connect();
    void disconnect();
    bool publish(const std::string &topic, const std::string &message, MachineInstance *machine);
    bool subscribe(const std::string &topic, MachineInstance *machine);
    bool publishes(const std::string &topic);
    bool subscribes(const std::string &topic);
    std::string getStateString(const std::string &topic);
    std::ostream &describe(std::ostream &out) const;

    int mid_sent;
    int last_mid;
    bool connected;
    char *username;
    char *password;
    bool disconnect_sent;
    bool quiet;

  protected:
    MQTTModule(const MQTTModule &other);
    MQTTModule &operator=(const MQTTModule &other);
};

#include <list>
#include <string>
#include <time.h>

class MQTTInterface {
  public:
    static int FREQUENCY;
    bool initialised;
    bool active;

    static MQTTInterface *instance();
    static void shutdown();

    // Timer
    static unsigned int sig_alarms;

    void collectState();
    void sendUpdates();

    bool start();
    bool stop();
    bool init();
    void add_topic(const char *name, const char *module_name, const char *topic);
    bool activate();
    bool addModule(MQTTModule *m, bool reset_io);
    bool online();
    bool operational();
    static std::list<MQTTModule *> modules;
    static MQTTModule *findModule(std::string module_name);
    static void processAll();

  private:
    MQTTInterface();
    ~MQTTInterface();
    static MQTTInterface *instance_;
    std::string id;
};

#endif
