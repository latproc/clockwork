#include "src/cw_interface.h"
#include "src/monitor.h"
#include "src/options.h"
#include <iostream>
#include <zmq.hpp>

void displayChanges(zmq::socket_t *sock, std::set<ModbusMonitor *> &changes, uint8_t *buffer_addr,
                    const Options &options) {
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

            if (sock && (mm->group() == 0 || mm->group() == 1) && mm->length() == 1) {
                sendStateUpdate(sock, mm, (bool)*val);
            }
        }
    }
}

void displayChanges(zmq::socket_t *sock, std::set<ModbusMonitor *> &changes, uint16_t *buffer_addr,
                    const Options &options) {
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
            if (mm->readOnly() && sock) { // INPUTREGISTER
                sendPropertyUpdate(sock, mm);
            }
        }
    }
}
