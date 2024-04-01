#include "options.h"
#include <cstdint>
#include <functional>
#include <set>
#include <zmq.hpp>

class ModbusMonitor;

void sendChanges(std::set<ModbusMonitor *> &changes, uint8_t *buffer_addr,
                    const Options &options,
                    std::function<void(ModbusMonitor *, bool)> send_state_update);

void sendChanges(std::set<ModbusMonitor *> &changes, uint16_t *buffer_addr,
                    const Options &options,
                    std::function<void(ModbusMonitor *)> send_property_update);

void sendStatus(const char *s);
void sendStateUpdate(zmq::socket_t *sock, ModbusMonitor *mm, bool which);
void sendPropertyUpdate(zmq::socket_t *sock, ModbusMonitor *mm);
