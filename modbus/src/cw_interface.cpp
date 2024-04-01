#include "cw_interface.h"
#include "monitor.h"
#include "options.h"
#include <iostream>

void sendChanges(std::set<ModbusMonitor *> &changes, uint8_t *buffer_addr,
                    const Options &options,
                    std::function<void(ModbusMonitor *, bool)> send_state_update) {
    if (changes.size()) {
        if (options.verbose)
            std::cerr << changes.size() << " changes\n";
        std::set<ModbusMonitor *>::iterator iter = changes.begin();
        while (iter != changes.end()) {
            ModbusMonitor *mm = *iter++;
            // note: the monitor address is in the global range grp<<16 + offset
            // this method is only using the addresses in the local range
            uint8_t *val = buffer_addr + ((mm->address() & 0xffff));
            if (options.verbose)
                std::cerr << mm->name() << " ";
            mm->set(val, options.verbose);

            if ((mm->group() == 0 || mm->group() == 1) && mm->length() == 1) {
                send_state_update(mm, (bool)*val);
            }
        }
    }
}

void sendChanges(std::set<ModbusMonitor *> &changes, uint16_t *buffer_addr,
                    const Options &options,
                    std::function<void(ModbusMonitor *)> send_property_update) {
    if (changes.size()) {
        if (options.verbose)
            std::cerr << changes.size() << " changes\n";
        std::set<ModbusMonitor *>::iterator iter = changes.begin();
        while (iter != changes.end()) {
            ModbusMonitor *mm = *iter++;
            uint16_t *val = buffer_addr + ((mm->address() & 0xffff));
            if (options.verbose)
                std::cerr << mm->name() << " ";
            mm->set(val, options.verbose);
            if (mm->readOnly()) { // INPUTREGISTER
                send_property_update(mm);
            }
        }
    }
}

