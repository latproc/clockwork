#include "src/options.h"
#include <cstdint>
#include <set>

namespace zmq {
class socket_t;
}

class ModbusMonitor;

void displayChanges(zmq::socket_t *sock, std::set<ModbusMonitor *> &changes, uint8_t *buffer_addr,
                    const Options &options);

void displayChanges(zmq::socket_t *sock, std::set<ModbusMonitor *> &changes, uint16_t *buffer_addr,
                    const Options &options);

void sendStatus(const char *s);
void sendStateUpdate(zmq::socket_t *sock, ModbusMonitor *mm, bool which);
void sendPropertyUpdate(zmq::socket_t *sock, ModbusMonitor *mm);
