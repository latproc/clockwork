#include "ProcessingThread.h"
#include "MachineInstance.h"
#include "Channel.h"
#include "IOComponent.h"
#include "ControlSystemMachine.h"
#include <boost/thread/recursive_mutex.hpp>

extern bool program_done;

void ProcessingThread:: wait_for_work(
    zmq::pollitem_t items[],
    ControlSystemMachine * machine,
    int & dynamic_poll_start_idx,
    uint64_t & curr_t,
    const int max_poll_sockets,
    int & poll_wait,
    bool & machines_have_work,
    int & systems_waiting,
    boost::recursive_mutex & runnable_mutex,
    uint64_t & last_machine_change,
    unsigned int num_channels,
    unsigned int machine_check_delay,
    zmq::socket_t & dispatch_sync,
    zmq::socket_t & sched_sync,
    zmq::socket_t & resource_mgr,
    zmq::socket_t & ecat_sync,
    zmq::socket_t & command_sync,
    zmq::socket_t & ecat_out,
    std::set<IOComponent *> & io_work_queue,
    uint64_t & last_checked_cycle_time,
    uint64_t & last_checked_plugins,
    uint64_t & last_checked_machines,
    uint64_t & last_sample_poll,
    const std::list<CommandSocketInfo*> &channels
    ){
        while (!program_done) {

            // add the channel sockets to our poll info
            {
                auto csi_iter = channels.begin();
                int idx = dynamic_poll_start_idx;
                while (csi_iter != channels.end()) {
                    CommandSocketInfo *info = *csi_iter++;
                    items[idx].socket = (void *)(*info->sock);
                    items[idx].fd = 0;
                    items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;
                    items[idx].revents = 0;
                    idx++;
                    if (idx == max_poll_sockets) {
                        break;
                    }
                }
                num_channels =
                    idx - dynamic_poll_start_idx; // the number channels we are actually monitoring
            }

            {
                static size_t last_runnable_count = 0;
                boost::recursive_mutex::scoped_lock lock(runnable_mutex);
                machines_have_work = !runnable.empty() || !MachineInstance::pendingEvents().empty();
                size_t runnable_count = runnable.size();
                if (runnable_count != last_runnable_count) {
                    //DBG_PROCESSING << "runnable: " << runnable_count << " (was " << last_runnable_count << ")\n";
                    last_runnable_count = runnable_count;
                }
            }
            if (machines_have_work || IOComponent::updatesWaiting() || !io_work_queue.empty()) {
                poll_wait = 1;
            }
            else {
                poll_wait = 100;
            }

            //if (Watchdog::anyTriggered(curr_t))
            //  Watchdog::showTriggered(curr_t, true, std::cerr);
            systems_waiting = pollZMQItems(poll_wait, items, 6 + num_channels, ecat_sync,
                                           resource_mgr, dispatch_sync, sched_sync, ecat_out);

            if (systems_waiting > 0 ||
                (machines_have_work && curr_t - last_checked_machines >= machine_check_delay)) {
                break;
            }
            if (IOComponent::updatesWaiting() || !io_work_queue.empty()) {
                break;
            }
            if (!MachineInstance::pluginMachines().empty() &&
                curr_t - last_checked_plugins >= 1000) {
                break;
            }
            if (curr_t - last_machine_change > 10000) {
                last_machine_change = curr_t;
                machine->idle();
            }
            if (last_machine_change < machine->lastUpdated()) {
                break;
            }
            if (machine->activationRequested()) { break; }
#ifdef KEEPSTATS
            avg_poll_time.update();
            usleep(1);
            avg_poll_time.start();
#endif
        }

}
