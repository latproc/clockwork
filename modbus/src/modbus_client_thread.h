#include "ConnectionManager.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "cJSON.h"
#include "monitor.h"
#include "plc_interface.h"
#include "src/buffer_monitor.h"
#include "src/cw_interface.h"
#include "src/modbus_helpers.h"
#include "src/options.h"
#include "symboltable.h"
#include <Logger.h>
#include <MessageEncoding.h>
#include <MessagingInterface.h>
#include <boost/thread.hpp>
#include <fstream>
#include <iostream>
#include <libgen.h>
#include <map>
#include <modbus.h>
#include <set>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <value.h>
#include <zmq.hpp>

class ModbusClientThread {
  private:
    modbus_t *ctx;
    boost::mutex update_mutex;
    boost::mutex work_mutex;

  public:
    uint8_t *tab_rp_bits;
    uint8_t *tab_ro_bits;
    uint16_t *tab_rq_registers;
    uint16_t *tab_rw_rq_registers;
    const Options &options;
    bool &update_status;

    bool finished;
    bool connected;

    BufferMonitor<uint8_t> bits_monitor;      //("bits", options);
    BufferMonitor<uint8_t> robits_monitor;    //("robits", options);
    BufferMonitor<uint16_t> regs_monitor;     //("input registers", options);
    BufferMonitor<uint16_t> holdings_monitor; //("holding registers", options);

    MonitorConfiguration &mc;
    const ModbusSettings &settings;

    zmq::socket_t *cmd_interface;
    const char *iod_cmd_socket_name;

    std::list<std::pair<int, bool>> bit_changes;
    void requestUpdate(int addr, bool which);

    std::list<std::pair<int, uint16_t>> register_changes;
    void requestRegisterUpdate(int addr, uint16_t val);

    void requestRegisterUpdates(int addr, uint16_t *vals, size_t count);

    modbus_t *getContext();
    void releaseContext();

    void performUpdates();

    void refresh();

    ModbusClientThread(const ModbusSettings &modbus_settings, MonitorConfiguration &modbus_config,
                       const Options &options, bool &update_status, const char *sock_name = 0);

    ~ModbusClientThread();
    modbus_t *openConnection();

    void close_connection();

    bool check_error(int rc, const char *msg, int entry, int *retry);
    int setBit(int addr, bool which);

    int setRegister(int addr, uint16_t val);

    int setRegisters(int addr, uint16_t *val, unsigned int n);

    bool collect_selected_updates(BufferMonitor<uint8_t> &bm, unsigned int grp, uint8_t *dest,
                                  std::map<std::string, ModbusMonitor> &entries,
                                  const char *fn_name,
                                  int (*read_fn)(modbus_t *ctx, int addr, int nb, uint8_t *dest));

    bool collect_selected_updates(BufferMonitor<uint16_t> &bm, unsigned int grp, uint16_t *dest,
                                  std::map<std::string, ModbusMonitor> &entries,
                                  const char *fn_name,
                                  int (*read_fn)(modbus_t *ctx, int addr, int nb, uint16_t *dest));

    void operator()();
};
