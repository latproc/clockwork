void ProcessingThread::handle_machines(
        uint64_t & last_checked_machines,
        unsigned int & machine_check_delay,
        ProcessingStates &processing_state,
        uint64_t & curr_t
    ) {

    if (processing_state == ProcessingStates::eIdle) {
        processing_state = ProcessingStates::ePollingMachines;
    }
    const int num_loops = 1;
    for (int i = 0; i < num_loops; ++i) {
        if (processing_state == ProcessingStates::ePollingMachines) {
#ifdef KEEPSTATS
            avg_clockwork_time.start();
#endif
            std::set<MachineInstance *> to_process;
            {
                boost::recursive_mutex::scoped_lock lock(runnable_mutex);
                std::set<MachineInstance *>::iterator iter = runnable.begin();
                while (iter != runnable.end()) {
                    MachineInstance *mi = *iter;
                    if (mi->executingCommand() || !mi->pendingEvents().empty() ||
                        mi->hasMail()) {
                        to_process.insert(mi);
                        if (!mi->queuedForStableStateTest()) {
                            iter = runnable.erase(iter);
                        }
                        else {
                            iter++;
                        }
                    }
                    else {
                        iter++;
                    }
                }
            }

            if (!to_process.empty()) {
                DBG_SCHEDULER << "processing " << to_process.size() << " machines\n";
                MachineInstance::processAll(to_process, 150000,
                                            MachineInstance::NO_BUILTINS);
            }
            processing_state = ProcessingStates::eStableStates;
        }
        if (processing_state == ProcessingStates::eStableStates) {
            std::set<MachineInstance *> to_process;
            {
                boost::recursive_mutex::scoped_lock lock(runnable_mutex);
                std::set<MachineInstance *>::iterator iter = runnable.begin();
                while (iter != runnable.end()) {
                    MachineInstance *mi = *iter;
                    if (mi->executingCommand() || !mi->pendingEvents().empty()) {
                        iter++;
                        continue;
                    }
                    if (mi->queuedForStableStateTest()) {
                        to_process.insert(mi);
                        iter = runnable.erase(iter);
                    }
                    else {
                        iter++;
                    }
                }
            }

            if (!to_process.empty()) {
                DBG_SCHEDULER << "processing stable states\n";
                MachineInstance::checkStableStates(to_process, 150000);
            }
            if (i < num_loops - 1) {
                processing_state = ProcessingStates::ePollingMachines;
            }
            else {
                processing_state = ProcessingStates::eIdle;
                last_checked_machines = curr_t; // check complete
#ifdef KEEPSTATS
                avg_clockwork_time.update();
#endif
            }
        }
    }
}
