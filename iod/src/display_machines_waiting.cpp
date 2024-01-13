#if 0
// debug code to work out what machines or systems tend to need processing
{
    if (systems_waiting > 0 || !io_work_queue.empty() || (machines_have_work || processing_state != ProcessingStates::eIdle || status != e_waiting))
    {
        DBG_PROCESSING << "handling activity. zmq: " << systems_waiting << " state: " << processing_state << " substate: " << status
                << ((items[internals->ECAT_ITEM].revents & ZMQ_POLLIN) ? " ethercat" : "")
                << ((IOComponent::updatesWaiting()) ? " io components" : "")
                << ((!io_work_queue.empty()) ? " io work" : "")
                << ((machines_have_work) ? " machines" : "")
                << ((!MachineInstance::pluginMachines().empty() && curr_t - last_checked_plugins >= 1000) ? " plugins" : "")
                << "\n";
    }
    if (IOComponent::updatesWaiting())
    {
        extern std::set<IOComponent *> updatedComponentsOut;
        std::set<IOComponent *>::iterator iter = updatedComponentsOut.begin();
        std::cout << updatedComponentsOut.size() << " entries in updatedComponentsOut:\n";
        while (iter != updatedComponentsOut.end()) {
            std::cout << " " << (*iter++)->io_name;
        }
        std::cout << " \n";
    }
}
#endif


