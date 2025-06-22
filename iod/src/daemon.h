#pragma once

#include <string>
#include <boost/optional.hpp>
#include <SharedQueueManager.h>
#include <zmq.hpp>
#include <DebugExtra.h>
#include "ClientInterface.h"
#include "ProcessingThread.h"
#include "ControlSystemMachine.h"
#include <boost/thread.hpp>
#include "ZmqNotifier.h"

struct UserData {
    std::string name;
    std::string value;
};

struct Daemon {
    Daemon(const std::string & program_name, const std::string &name, UserData *user_data = nullptr);

    void do_not_export_internal_properties();
    void start(HardwareActivation *arctivation, UserData *user_data = nullptr);
    void stop();

    std::string thread_name;
    UserData *user_data = nullptr;
    bool initialized = false;
    DebugExtra *dbg_instance = nullptr;
    SharedQueueManager queue_manager;
    zmq::context_t *context = nullptr;
    IODCommandThread *stateMonitor = nullptr;
    boost::optional<boost::thread> scheduler_thread;
    boost::optional<boost::thread> process;
    boost::optional<boost::thread> monitor;
    ProcessingThread *processMonitor = nullptr;
    ControlSystemMachine machine;
    boost::condition_variable_any processing_condition;
    boost::shared_mutex processing_queue_mutex;
    boost::condition_variable_any refresh_condition;
    boost::shared_mutex refresh_queue_mutex;
    boost::condition_variable_any mqtt_source_condition;
    boost::shared_mutex mqtt_source_queue_mutex;
    boost::condition_variable_any scheduler_condition;
    boost::shared_mutex scheduler_queue_mutex;
    const std::string shared_queue_notification_endpoint = "inproc://shared_queues";
    ZmqNotifier notify_zmq;
    zmq::socket_t queue_notification; // recieves notifications from shared queues
};

boost::optional<Daemon> create_clockwork_daemon(int argc, char const *argv[], const std::string &thread_name);

void display_dependency_graph();
int export_modbus_mappings(int result);
