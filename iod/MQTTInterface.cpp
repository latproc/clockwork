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

#include <iostream>
#include <sys/time.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <mosquitto.h>
#include "MQTTInterface.h"
#include "IOComponent.h"
#include <boost/thread/condition.hpp>
#include "MachineInstance.h"

void mqttif_signal_handler(int signum);

std::list<MQTTModule *> MQTTInterface::modules;

int MQTTInterface::FREQUENCY = 100;

void my_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    MQTTModule *device = (MQTTModule *)obj;
    
    if(message->payloadlen){
        std::cout << message->topic << ": " << (const char *)message->payload << "\n";
        std::map<std::string, MachineInstance*>::iterator pos = device->handlers.find(message->topic);
        if (pos != device->handlers.end()) {
            MachineInstance *m = (*pos).second;
            m->setValue("topic", message->topic);
            m->setValue("message", (const char *)message->payload);
            const char *evt = (const char *)message->payload;
            std::string event("");
            event += evt;
            event += "_enter";
            if (m->_type == "POINT" && (event ==  "on_enter" || event == "off_enter")) {
                Message msg(event.c_str());
                m->execute(msg, device);
            }
            else {
                std::set<MachineInstance*>::iterator iter = m->depends.begin();
                while (iter != m->depends.end()) {
                    MachineInstance *mi = *iter++;
                    Message msg(event.c_str());
                    mi->execute(msg, device);
                }
			}
        }
    }
}

void my_connect_callback(struct mosquitto *mosq, void *obj, int result)
{
    MQTTModule *device = (MQTTModule *)obj;
    
	if(result){
        device->last_error_str = mosquitto_connack_string(result);
        device->last_error = result;
    }
}

void my_disconnect_callback(struct mosquitto *mosq, void *obj, int rc)
{
    MQTTModule *device = (MQTTModule *)obj;
	device->connected = false;
}

void my_publish_callback(struct mosquitto *mosq, void *obj, int mid)
{
    MQTTModule *device = (MQTTModule *)obj;
}


void my_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
    MQTTModule *device = (MQTTModule *)obj;
    
    std::cout << "Subscribed (mid: " << mid << "): "<< granted_qos[0];
	for(int i=1; i<qos_count; i++){
        std::cout << ", " << granted_qos[i];
	}
    std::cout << "\n";
}


void my_subscribe_callback(struct mosquitto *mosq, void *obj, int mid)
{
    
}

MQTTModule::MQTTModule() {
    mid_sent = 0;
    last_mid = -1;
    connected = true;
    username = NULL;
    password = NULL;
    disconnect_sent = false;
    quiet = false;

    mosq = mosquitto_new(NULL, true, this);
	if(!mosq){
		switch(errno){
			case ENOMEM:
				if(!quiet) fprintf(stderr, "Error: Out of memory.\n");
				break;
			case EINVAL:
				if(!quiet) fprintf(stderr, "Error: Invalid id.\n");
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
    mosquitto_connect(mosq, host.c_str(), port, 10);
    connected = true;
    return connected;
}

void MQTTModule::disconnect() {
    mosquitto_disconnect(mosq);
    disconnect_sent = true;
}

bool MQTTModule::publish(const std::string &topic, const std::string &message, MachineInstance *m) {
    pubs.add(topic.c_str(), message.c_str());
    const Value &msg_val = pubs.lookup(topic.c_str());
    int rc = mosquitto_publish(mosq, &mid_sent, topic.c_str(), (int)msg_val.asString().length(), msg_val.asString().c_str(), 0, true);
    if(rc){
        last_error = rc;
        switch(rc){
            case MOSQ_ERR_INVAL:
                last_error_str = "Error: Invalid input. Does your topic contain '+' or '#'?\n";
                status = STATUS_ERROR;
                break;
            case MOSQ_ERR_NOMEM:
                last_error_str =  "Error: Out of memory when trying to publish message.\n";
                break;
            case MOSQ_ERR_NO_CONN:
                last_error_str = "Error: Client not connected when trying to publish.\n";
                break;
            case MOSQ_ERR_PROTOCOL:
                last_error_str = "Error: Protocol error when communicating with broker.\n";
                break;
            case MOSQ_ERR_PAYLOAD_SIZE:
                last_error_str = "Error: Message payload is too large.\n";
                break;
        }
        return false;
    }
    m->setValue("topic", topic.c_str());
    m->setValue("message", message.c_str());
    handlers[topic] = m;
    m->mq_interface = this;
    return true;
}


bool MQTTModule::subscribe(const std::string &topic, MachineInstance *m) {
    subs.add(topic.c_str(), "");
    int rc = mosquitto_subscribe(mosq, NULL, topic.c_str(), 0);
    if(rc){
        last_error = rc;
        switch(rc){
            case MOSQ_ERR_INVAL:
                last_error_str = "Error: Invalid input. Does your topic contain '+' or '#'?\n";
                status = STATUS_ERROR;
                break;
            case MOSQ_ERR_NOMEM:
                last_error_str =  "Error: Out of memory when trying to publish message.\n";
                break;
            case MOSQ_ERR_NO_CONN:
                last_error_str = "Error: Client not connected when trying to publish.\n";
                break;
            case MOSQ_ERR_PROTOCOL:
                last_error_str = "Error: Protocol error when communicating with broker.\n";
                break;
        }
        return false;
    }
    handlers[topic] = m;
    m->mq_interface = this;
    return true;
}


std::string MQTTModule::getStateString(const std::string &topic)
{
    if ( pubs.exists(topic.c_str()) ) {
        return pubs.lookup(topic.c_str()).asString();
    }
    else return "";
}

bool MQTTModule::publishes(const std::string &topic)
{
    return pubs.exists(topic.c_str());
}

bool MQTTModule::subscribes(const std::string &topic)
{
    return subs.exists(topic.c_str());
}

MQTTInterface::MQTTInterface() :initialised(0), active(false) {
	initialised = init();
}

std::ostream &MQTTModule::describe(std::ostream &out) const {
    out << "Publishes: " << pubs << " Subscribes " << subs;
    return out;
}

MQTTInterface::~MQTTInterface() {
    mosquitto_lib_cleanup();
}

void MQTTInterface::shutdown() {
    delete instance_;
}

std::ostream &MQTTModule::operator <<(std::ostream & out)const {
	out << "Topic " << name;
	return out;
}


MQTTModule *MQTTInterface::findModule(std::string name) {
	std::list<MQTTModule *>::iterator iter = modules.begin();
	while (iter != modules.end()){
		MQTTModule *m = *iter++;
		if (m->name == name) return m;
	}
	return 0;
}

void MQTTInterface::processAll()
{
}

bool MQTTInterface::addModule(MQTTModule *module, bool reset_io) {
	MQTTModule *m = findModule(module->name);
    if (m) return false;

	modules.push_back(module);
	
	if (!reset_io) 
		return true;
    //tbd
	return true;
}

bool MQTTInterface::activate() {
    std::cerr << "Activating MQTT Interface...";
	active = true;
    std::cerr << "done\n";
	
    return true;
}

bool MQTTInterface::online() {
	std::list<MQTTModule *>::iterator iter = modules.begin();
	while (iter != modules.end()){
		MQTTModule *m = *iter++;
		if (!m->online()) return false;
	}
	return true;
}

bool MQTTInterface::operational() {
	return true;
}

bool MQTTInterface::init() {
    
    if(initialised) return true;
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

MQTTInterface* MQTTInterface::instance_ = 0;

MQTTInterface *MQTTInterface::instance() {
	if (!instance_) instance_ = new MQTTInterface();
	return instance_;
}

void MQTTInterface::collectState() {
	if (!initialised || !active) {
		return;
	}
    std::list<MQTTModule *>::iterator iter = modules.begin();
    while (iter != modules.end()) {
        MQTTModule *module = *iter++;
        int rc = mosquitto_loop(module->mosq, -1, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::cout << "Error: " << rc << " polling mosquitto\n";
            module->connect();
        }
    }
}

void MQTTInterface::sendUpdates() {
	if (!initialised || !active) {
		return;
	}
}


bool MQTTInterface::start() {
#if 0
    struct sigaction sa;
    struct itimerval tv;
    sa.sa_handler = mqttif_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, 0)) {
		std::cerr<< "Failed to install signal handler!\n";
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
