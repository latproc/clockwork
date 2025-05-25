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

#include "MQTTInterface.h"
#include "DebugExtra.h"
#include "IOComponent.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include <boost/thread/condition.hpp>
#include <errno.h>
#include <iomanip>
#include <iostream>
#include <mosquitto.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "Dispatcher.h"

void mqttif_signal_handler(int signum);

std::list<MQTTModule *> MQTTInterface::modules;

int MQTTInterface::FREQUENCY = 100;

void MQTTInterface::enqueReceivedMessage(MQTTReceivedMessage *msg) {
    sink->enqueue(msg);
}

void my_message_callback(struct mosquitto *mosq, void *obj,
                         const struct mosquitto_message *message) {
    MQTTModule *device = (MQTTModule *)obj;

    if (message && message->payloadlen) {
        char *payload = new char[message->payloadlen + 1];
        memcpy(payload, message->payload, message->payloadlen);
        payload[message->payloadlen] = 0;
        {
            std::stringstream ss;
            ss << "MQTT message received: " << payload;
            MessageLog::instance()->add(ss.str().c_str());
        }
        auto to_send = new MQTTInterface::MQTTReceivedMessage({
            device,
            message->topic,
            payload
        });
        delete[] payload;
        MQTTInterface::instance()->enqueReceivedMessage(to_send);
    }
    else {
        MessageLog::instance()->add("MQTT callback with no payload");
    }
}

void my_connect_callback(struct mosquitto *mosq, void *obj, int result) {
    MQTTModule *device = (MQTTModule *)obj;

    if (result != MOSQ_ERR_SUCCESS) {
        std::stringstream ss;
        ss << "MQTT Connection Error: " << mosquitto_connack_string(result);
        MessageLog::instance()->add(ss.str());
        device->last_error_str = mosquitto_connack_string(result);
        device->last_error = result;
        device->connected = false;
    }
    else {
        MessageLog::instance()->add("Connected to MQTT broker");
        device->connected = true;
    }
}

void my_disconnect_callback(struct mosquitto *mosq, void *obj, int rc) {
    MQTTModule *device = (MQTTModule *)obj;
    device->connected = false;
    if (rc != 0) {
        MessageLog::instance()->add("Unexpected disconnect: " +
                                    std::string(mosquitto_strerror(rc)));
    }
    else {
        MessageLog::instance()->add("Disconnected from MQTT broker");
    }
}

void my_publish_callback(struct mosquitto *mosq, void *obj, int mid) {
    MQTTModule *device = (MQTTModule *)obj;
}

void my_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count,
                           const int *granted_qos) {
    MQTTModule *device = (MQTTModule *)obj;
    std::stringstream ss;
    ss << "Subscribed (mid: " << mid << "): qos: " << granted_qos[0];
    for (int i = 1; i < qos_count; i++) {
        ss << ", " << granted_qos[i];
    }
    MessageLog::instance()->add(ss.str());
}

void my_subscribe_callback(struct mosquitto *mosq, void *obj, int mid) {
    assert(false);
}

MQTTModule::MQTTModule(const char *name) : Transmitter(name) {
    mid_sent = 0;
    last_mid = -1;
    connected = false;
    disconnect_sent = false;
    quiet = false;

    mosq = mosquitto_new(getName().c_str(), true, this);
    if (!mosq) {
        switch (errno) {
        case ENOMEM:
            MessageLog::instance()->add("MQTT initialisation error: out of memory.");
            break;
        case EINVAL:
            MessageLog::instance()->add("MQTT initialisation error: invalid input.");
            break;
        }
    }
    else {
        mosquitto_connect_callback_set(mosq, my_connect_callback);
        mosquitto_subscribe_callback_set(mosq, my_subscribe_callback);
        mosquitto_disconnect_callback_set(mosq, my_disconnect_callback);
        mosquitto_publish_callback_set(mosq, my_publish_callback);
        mosquitto_message_callback_set(mosq, my_message_callback);
    }
}

bool MQTTModule::online() {
    return true; //tbd
}

bool MQTTModule::connect() {
    if (username && password) {
        auto result = mosquitto_username_pw_set(mosq, username->c_str(), password->c_str());
        if (result != MOSQ_ERR_SUCCESS) {
            last_error = result;
            last_error_str = "Error: Could not set username and password.";
            MessageLog::instance()->add(last_error_str);
            return false;
        }
    }
    auto result = mosquitto_connect(mosq, host.c_str(), port, 20);
    if (result != MOSQ_ERR_SUCCESS) {
        last_error = result;
        last_error_str = "Error: Could not connect to broker.";
        MessageLog::instance()->add(last_error_str);
        return false;
    }
    connected = true;
    return true;
}

void MQTTModule::disconnect() {
    mosquitto_disconnect(mosq);
    disconnect_sent = true;
}

bool MQTTModule::publish(const std::string &topic, const std::string &message, MachineInstance *m) {
    pubs.add(topic.c_str(), message.c_str());
    const Value &msg_val = pubs.lookup(topic.c_str());
    int rc = mosquitto_publish(mosq, &mid_sent, topic.c_str(), (int)msg_val.asString().length(),
                               msg_val.asString().c_str(), 0, true);
    if (rc) {
        last_error = rc;
        switch (rc) {
        case MOSQ_ERR_INVAL:
            last_error_str = "Error: Invalid input. Does your topic contain '+' or '#'?";
            status = STATUS_ERROR;
            break;
        case MOSQ_ERR_NOMEM:
            last_error_str = "Error: Out of memory when trying to publish message.";
            break;
        case MOSQ_ERR_NO_CONN:
            last_error_str = "Error: Client not connected when trying to publish.";
            break;
        case MOSQ_ERR_PROTOCOL:
            last_error_str = "Error: Protocol error when communicating with broker.";
            break;
        case MOSQ_ERR_PAYLOAD_SIZE:
            last_error_str = "Error: Message payload is too large.";
            break;
        }
        if (rc != MOSQ_ERR_SUCCESS) {
            MessageLog::instance()->add(last_error_str);
        }
        return false;
    }
    m->setValue("topic", Value(topic.c_str(), Value::t_string));
    if (m->getValue("message").asString() != message) {
        m->setValue("message", Value(message.c_str(), Value::t_string));
    }
    handlers[topic] = m;
    m->mq_interface = this;
    return true;
}

bool MQTTModule::subscribe(const std::string &topic, MachineInstance *m) {
    subs.add(topic.c_str(), "");
    int rc = mosquitto_subscribe(mosq, NULL, topic.c_str(), 0);
    if (rc) {
        last_error = rc;
        switch (rc) {
        case MOSQ_ERR_INVAL:
            last_error_str = "MQTT Error: Invalid input. Does your topic contain '+' or '#'?";
            status = STATUS_ERROR;
            break;
        case MOSQ_ERR_NOMEM:
            last_error_str = "MQTT Error: Out of memory when trying to subscribe";
            break;
        case MOSQ_ERR_NO_CONN:
            last_error_str = "MQTT Error: Client not connected when trying to subscribe";
            break;
        case MOSQ_ERR_PROTOCOL:
            last_error_str = "MQTT Error: Protocol error when communicating with broker.";
            break;
        case MOSQ_ERR_PAYLOAD_SIZE:
            last_error_str = "MQTT Error: invalid payload size when communicating with broker.";
        }
        if (rc != MOSQ_ERR_SUCCESS) {
            MessageLog::instance()->add(last_error_str);
        }
        return false;
    }
    handlers[topic] = m;
    m->mq_interface = this;
    return true;
}

std::string MQTTModule::getStateString(const std::string &topic) {
    if (pubs.exists(topic.c_str())) {
        return pubs.lookup(topic.c_str()).asString();
    }
    else {
        return "";
    }
}

MachineInstance *MQTTModule::find_handler(const std::string &topic) {
    auto found = handlers.find(topic);
    if (found != handlers.end()) {
        return (*found).second;
    }
    return nullptr;
}

bool MQTTModule::publishes(const std::string &topic) { return pubs.exists(topic.c_str()); }

bool MQTTModule::subscribes(const std::string &topic) { return subs.exists(topic.c_str()); }

MQTTInterface::MQTTInterface() : initialised(0), active(false), sink(nullptr) { initialised = init(); }

std::ostream &MQTTModule::describe(std::ostream &out) const {
    out << "Publishes: " << pubs << " Subscribes " << subs;
    return out;
}

MQTTInterface::~MQTTInterface() { mosquitto_lib_cleanup(); }

void MQTTInterface::shutdown() { delete instance_; }

std::ostream &MQTTModule::operator<<(std::ostream &out) const {
    out << "Topic " << _name;
    return out;
}

MQTTModule *MQTTInterface::findModule(std::string name) {
    std::list<MQTTModule *>::iterator iter = modules.begin();
    while (iter != modules.end()) {
        MQTTModule *m = *iter++;
        if (m->getName() == name) {
            return m;
        }
    }
    return 0;
}

void MQTTInterface::processAll() {}

bool MQTTInterface::addModule(MQTTModule *module, bool reset_io) {
    MQTTModule *m = findModule(module->getName());
    if (m) {
        return false;
    }

    modules.push_back(module);

    if (!reset_io) {
        return true;
    }
    //tbd
    return true;
}

bool MQTTInterface::activate() {
    DBG_INITIALISATION << "Activating MQTT Interface...";
    active = true;

    return true;
}

bool MQTTInterface::online() {
    std::list<MQTTModule *>::iterator iter = modules.begin();
    while (iter != modules.end()) {
        MQTTModule *m = *iter++;
        if (!m->online()) {
            return false;
        }
    }
    return true;
}

bool MQTTInterface::operational() { return true; }

bool MQTTInterface::init() {

    if (initialised) {
        return true;
    }
    mosquitto_lib_init();
    initialised = true;
    return true;
}

// Timer
unsigned int MQTTInterface::sig_alarms = 0;

void mqttif_signal_handler(int signum) {
    switch (signum) {
    case SIGALRM:
        MQTTInterface::sig_alarms++;
        break;
    default:
        std::cerr << "Signal: " << signum << "\n" << std::flush;
    }
}

MQTTInterface *MQTTInterface::instance_ = 0;

MQTTInterface *MQTTInterface::instance() {
    if (!instance_) {
        instance_ = new MQTTInterface();
    }
    return instance_;
}

#if 0
void MQTTInterface::collectState() {
    if (!initialised || !active) {
        return;
    }
    std::list<MQTTModule *>::iterator iter = modules.begin();
    while (iter != modules.end()) {
        MQTTModule *module = *iter++;
        if (module->connected) {
            int rc = mosquitto_loop(module->mosq, -1, 1);
            if (rc != MOSQ_ERR_SUCCESS) {
                std::stringstream ss;
                ss << "Error: " << mosquitto_strerror(rc) << " polling mosquitto";
                MessageLog::instance()->add(ss.str());
            }
        }
        else if (!module->connect()) {
            std::stringstream ss;
            ss << "Error: " << mosquitto_strerror(module->last_error) << ": "
               << module->last_error_str;
            MessageLog::instance()->add(ss.str());
        }
    }
}

void MQTTInterface::sendUpdates() {
    if (!initialised || !active) {
        return;
    }
}
#endif

void setup_mqtt(const std::map<std::string, MachineInstance *> &machines) {
    {
        size_t remaining = machines.size();
        DBG_PARSER << remaining << " Machines\n";
        DBG_INITIALISATION << "Initialising MQTT\n";

        // find and process all MQTT Modules as they are required before POINTS that use them
        std::map<std::string, MachineInstance *>::const_iterator iter = machines.begin();
        while (iter != machines.end()) {
            MachineInstance *m = (*iter).second;
            iter++;
            if (m->_type == "MQTTBROKER" &&
                (m->parameters.size() == 2 || m->parameters.size() == 4)) {
                MQTTModule *module = MQTTInterface::instance()->findModule(m->getName());
                if (module) {
                    std::stringstream ss;
                    ss << "MQTT Broker " << m->getName() << " is already registered";
                    MessageLog::instance()->add(ss.str());
                    continue;
                }
                module = new MQTTModule(m->getName().c_str());
                module->host = m->parameters[0].val.asString();
                if (m->parameters.size() == 4) {
                    module->username = m->parameters[2].val.asString();
                    module->password = m->parameters[3].val.asString();
                }
                int64_t port;
                if (m->parameters[1].val.asInteger(port)) {
                    module->port = (int)port;
                    MQTTInterface::instance()->addModule(module, false);
                    module->connect();
                }
            }
        }

        iter = machines.begin();
        while (iter != machines.end()) {
            MachineInstance *m = (*iter).second;
            iter++;
            --remaining;
            if (m->_type == "MQTTBROKER" &&
                (m->parameters.size() == 2 || m->parameters.size() == 4)) {
                continue;
            }
            else if (m->_type == "POINT" && m->parameters.size() > 1 &&
                     m->parameters[1].val.kind == Value::t_integer) {
                // POINTs with integer values are used by EtherCAT
#if 0
                std::string name = m->parameters[0].real_name;
                //int bit_position = (int)m->parameters[1].val.iValue;
                //std::cerr << "Setting up point " << m->getName() << " " << bit_position << " on module " << name << "\n";
                MachineInstance *module_mi = MachineInstance::find(name.c_str());
                if (!module_mi) {
                    std::cerr << "No machine called " << name << "\n";
                    continue;
                }
                if (!module_mi->properties.exists("position")) { // module position not given
                    std::cerr << "Machine " << name << " does not specify a position\n";
                    continue;
                }
                int module_position = (int)module_mi->properties.lookup("position").iValue;
                if (module_position == -1) { // module position unmapped
                    std::cerr << "Machine " << name << " position not mapped\n";
                    continue;
                }
#else
                continue;
#endif
            }
            else {
                if (m->_type == "POINT" || m->_type == "MQTTPUBLISHER" ||
                    m->_type == "MQTTSUBSCRIBER") {
                    std::string name = m->parameters[0].real_name;
                    DBG_INITIALISATION << "Setting up " << m->_type << " " << m->getName() << " \n";
                    MQTTModule *module =
                        MQTTInterface::instance()->findModule(m->parameters[0].real_name);
                    if (!module) {
                        std::stringstream ss;
                        ss << "No MQTT Broker called " << m->parameters[0].real_name;
                        MessageLog::instance()->add(ss.str());
                        continue;
                    }
                    std::string topic = m->parameters[1].val.asString();
                    if (m->_type != "MQTTSUBSCRIBER" && m->parameters.size() == 3) {
                        if (!module->publishes(topic)) {
                            m->properties.add("type", "Output");
                            module->publish(topic, m->parameters[2].val.asString(), m);
                        }
                    }
                    else if (m->_type != "MQTTPUBLISHER") {
                        if (!module->subscribes(topic)) {
                            m->properties.add("type", "Input");
                            module->subscribe(topic, m);
                        }
                    }
                    else {
                        MessageLog::instance()->add("Error defining instance " + m->getName());
                    }
                }
            }
        }
        assert(remaining == 0);
    }
}

bool MQTTInterface::start(SharedThreadSafeQueue<MQTTReceivedMessage*> &sink) {
#if 0
    struct sigaction sa;
    struct itimerval tv;
    sa.sa_handler = mqttif_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, 0)) {
        std::cerr << "Failed to install signal handler!\n";
        return false;
    }

    if (FREQUENCY > 1) {
        tv.it_interval.tv_sec = 0;
        tv.it_interval.tv_usec = 1000000 / FREQUENCY;
    }
    else {
        tv.it_interval.tv_sec = 1;
        tv.it_interval.tv_usec = 0;
    }
    tv.it_value.tv_sec = 0;
    tv.it_value.tv_usec = 1000;
    if (setitimer(ITIMER_REAL, &tv, NULL)) {
        std::cerr << "Failed to start timer: " << strerror(errno) << "\n";
        return false;
    }
#endif
    instance()->sink = &sink;
    setup_mqtt(machines);
    std::list<MQTTModule *>::iterator iter = modules.begin();
    while (iter != modules.end()) {
        MQTTModule *module = *iter++;
        mosquitto_loop_start(module->mosq);
    }
    return true;
}

bool MQTTInterface::stop() {
#if 0
    struct itimerval tv;
    tv.it_interval.tv_sec = 0;
    tv.it_interval.tv_usec = 0;
    tv.it_value.tv_sec = 0;
    tv.it_value.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &tv, NULL)) {
        std::cerr << "Failed to stop timer: " << strerror(errno) << "\n";
        return false;
    }
#endif
    std::list<MQTTModule *>::iterator iter = modules.begin();
    while (iter != modules.end()) {
        MQTTModule *module = *iter++;
        mosquitto_loop_stop(module->mosq, false);
    }
    return true;
}
