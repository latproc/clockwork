#include "MachineInstance.h"
#include "ProcessingThread.h"
#include <stdint.h>
#include "DebugExtra.h"
#include "MessageLog.h"

void ProcessingThread::handle_machines(
        uint64_t & last_checked_machines,
        unsigned int & machine_check_delay,
        uint64_t & curr_t
    ) {
    const int num_loops = 2;
    for (int i = 0; i < num_loops; ++i) {
#ifdef KEEPSTATS
        avg_clockwork_time.start();
#endif
        std::set<MachineInstance *> to_poll_actions;
        std::set<MachineInstance *> to_poll_stable_states;
        {
            boost::recursive_mutex::scoped_lock lock(runnable_mutex);
            std::set<MachineInstance *>::iterator iter = runnable.begin();
            while (iter != runnable.end()) {
                MachineInstance *mi = *iter;
                if (!mi->is_runnable()) {
                    std::stringstream ss;
                    ss << "Machine " << mi->getName() << " is not runnable but was in the runnable list\n";
                    MessageLog::instance()->add(ss.str().c_str());
                    continue;
                }
                if (mi->executingCommand() || !mi->pendingEvents().empty() || mi->hasMail()) {
                    to_poll_actions.insert(mi);
                }
                else {
                    to_poll_stable_states.insert(mi);
                }
                iter = runnable.erase(iter);
            }
            // TODO: Is this catch-all necessary?
            for (auto i = MachineInstance::begin(); i != MachineInstance::end(); ++i) {
                auto mi = *i;
                if (mi->is_runnable()) {
                   if (mi->executingCommand() || !mi->pendingEvents().empty() || mi->hasMail()) {
                       to_poll_actions.insert(mi);
                   }
                   else {
                       to_poll_stable_states.insert(mi);
                   }
                }
            }
        }

        if (!to_poll_actions.empty()) {
            MachineInstance::processAll(to_poll_actions, 150000,
                                        MachineInstance::NO_BUILTINS);
        }

        if (!to_poll_stable_states.empty()) {
            MachineInstance::checkStableStates(to_poll_stable_states, 150000);
        }

        last_checked_machines = curr_t; // check complete
#ifdef KEEPSTATS
        avg_clockwork_time.update();
#endif
    }
}
