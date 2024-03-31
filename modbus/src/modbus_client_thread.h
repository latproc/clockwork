#include <iostream>
#include <zmq.hpp>
#include <modbus.h>
#include <unistd.h>
#include <stdlib.h>
#include <boost/thread.hpp>
#include <map>
#include <set>
#include "plc_interface.h"
#include <string.h>
#include "monitor.h"
#include <zmq.hpp>
#include <value.h>
#include <MessageEncoding.h>
#include <MessagingInterface.h>
#include "cJSON.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "ConnectionManager.h"
#include "symboltable.h"
#include <fstream>
#include <libgen.h>
#include <sys/time.h>
#include <Logger.h>
#include "src/options.h"
#include "src/buffer_monitor.h"
#include "src/cw_interface.h"
#include "src/modbus_helpers.h"

class ModbusClientThread{
private:
    modbus_t *ctx;
	boost::mutex update_mutex;
	boost::mutex work_mutex;

public:
    uint8_t *tab_rq_bits;
    uint8_t *tab_rp_bits;
    uint8_t *tab_ro_bits;
    uint16_t *tab_rq_registers;
    uint16_t *tab_rw_rq_registers;
    const Options &options;
    bool & update_status;

	bool finished;
	bool connected;
	
	BufferMonitor<uint8_t> bits_monitor; //("bits", options);
	BufferMonitor<uint8_t> robits_monitor; //("robits", options);
	BufferMonitor<uint16_t> regs_monitor; //("input registers", options);
	BufferMonitor<uint16_t> holdings_monitor; //("holding registers", options);

	MonitorConfiguration &mc;
	const ModbusSettings &settings;

	zmq::socket_t *cmd_interface;
	const char *iod_cmd_socket_name;

	std::list< std::pair<int, bool> >bit_changes;
	void requestUpdate(int addr, bool which);

	std::list< std::pair<int, uint16_t> >register_changes;
	void requestRegisterUpdate(int addr, uint16_t val);

	void requestRegisterUpdates(int addr, uint16_t *vals, size_t count);

	modbus_t *getContext();
	void releaseContext();

	void performUpdates();
	
	void refresh();

    ModbusClientThread(const ModbusSettings &modbus_settings, MonitorConfiguration &modbus_config,
					   const Options & options,
                       bool & update_status,
                       const char *sock_name = 0);

~ModbusClientThread();
modbus_t *openConnection();

void close_connection();

bool check_error(const char *msg, int entry, int *retry);
bool setBit(int addr, bool which);

bool setRegister(int addr, uint16_t val);

bool setRegisters(int addr, uint16_t *val, unsigned int n);

template<typename T>bool collect_selected_updates(BufferMonitor<T> &bm, unsigned int grp, T *dest, 
	std::map<std::string, ModbusMonitor>&entries,
	const char *fn_name,
	int (*read_fn)(modbus_t *ctx, int addr, int nb, T *dest)) {

	if (entries.empty()) return true;
	int rc = 0;
	int min = 100000;
	int max = 0;
	std::map<std::string, ModbusMonitor>::const_iterator iter = entries.begin();
	while (iter != entries.end()) {
		const std::pair<std::string, ModbusMonitor> &item = *iter++;
		if (item.second.group() == grp) {
			int offset = item.second.address();
			int end = offset + item.second.length() - 1;
			if (offset < min) min = offset;
			if (end > max) max = end;
			int retry = 2;
			while ( (usleep(5000), rc = read_fn(ctx, offset, item.second.length(), dest+offset)) == -1 ) {
			    if (options.verbose)
					std::cerr << "called: read_fn(ctx, " << offset << ", " 
						<< item.second.length() << ", " << dest+offset << "))\n";
				check_error(fn_name, offset, &retry); 
				if (!connected) return false;
				if (--retry>0) continue; else break;
			}
		}
	}
	if (!connected) { std::cerr << "Lost connection\n"; return false; }
	if (min>max) return true;
	std::set<ModbusMonitor*> changes;
	bm.check((max-min+1), dest+min, (grp<<16) + min, changes);
	displayChanges(cmd_interface, changes, dest, options);
	return true;
}

void operator()();
};

