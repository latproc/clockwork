#if 0
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
#endif

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
	void requestUpdate(int addr, bool which) {
		boost::mutex::scoped_lock lock(work_mutex);
		bit_changes.push_back(std::make_pair(addr, which));
	}

	std::list< std::pair<int, uint16_t> >register_changes;
	void requestRegisterUpdate(int addr, uint16_t val) {
		boost::mutex::scoped_lock lock(work_mutex);
		register_changes.push_back(std::make_pair(addr, val));
	}

	void requestRegisterUpdates(int addr, uint16_t *vals, size_t count) {
		boost::mutex::scoped_lock lock(work_mutex);
		for (size_t i = 0; i<count; ++i) {
			register_changes.push_back(std::make_pair(addr + i, *vals++));
		}
	}

	modbus_t *getContext() {
		update_mutex.lock();
		return ctx;
	}
	void releaseContext() {
		update_mutex.unlock();
	}

	void performUpdates() {
		boost::mutex::scoped_lock lock(work_mutex);
		{
			std::list< std::pair<int, bool> >::iterator iter = bit_changes.begin();
			while (iter != bit_changes.end()) {
				std::pair<int, bool>item = *iter;
				setBit(item.first, item.second);
				iter = bit_changes.erase(iter);
			}
		}
		{
			std::list< std::pair<int, uint16_t> >::iterator iter = register_changes.begin();
			if (iter == register_changes.end())
				return;
			unsigned int space=10;
			uint16_t *vals = (uint16_t*)malloc(space * sizeof(uint16_t));
			unsigned int n = 0;
			int start = 0;
			int last = 0;
			while (iter != register_changes.end()) {
				std::pair<int, uint16_t>item = *iter;
				if (n==0) {
					start = item.first;
				}
				else if (item.first != last+1) {
					//flush
					setRegisters(start, vals, n);
					start = item.first;
					n = 0;
				}
				last = item.first;
				vals[n++] = item.second;
				if (n == space) {
					space += 10;
					vals = (uint16_t*)realloc(vals, space * sizeof(uint16_t));
				}
				iter = register_changes.erase(iter);
			}
			if (n) {
				setRegisters(start, vals, n);
			}
			free(vals);
		}
	}
	
	void refresh() {
		bits_monitor.refresh();
		robits_monitor.refresh();
		regs_monitor.refresh();
		holdings_monitor.refresh();
	}

	ModbusClientThread(const ModbusSettings &modbus_settings, MonitorConfiguration &modbus_config,
					   const char *sock_name = 0) :
			ctx(0), tab_rq_bits(0), tab_rp_bits(0), tab_ro_bits(0),
			tab_rq_registers(0), tab_rw_rq_registers(0), 
			finished(false), connected(false),
			bits_monitor("coils", options), 
			robits_monitor("discrete", options),
			regs_monitor("registers", options),
			holdings_monitor("holdings", options), mc(modbus_config),
			settings(modbus_settings),
			cmd_interface(0), iod_cmd_socket_name(sock_name)
	{
		boost::mutex::scoped_lock lock(update_mutex);

		ctx = openConnection();
		if (!ctx) {
			std::cerr << "failed to create modbus context\n";
			exit(1);
		}
		modbus_set_debug(ctx, FALSE);

    /* Allocate and initialize the different memory spaces */
	int nb = 10000;

    tab_rq_bits = (uint8_t *) malloc(nb * sizeof(uint8_t));
    memset(tab_rq_bits, 0, nb * sizeof(uint8_t));

    tab_rp_bits = (uint8_t *) malloc(nb * sizeof(uint8_t));
    memset(tab_rp_bits, 0, nb * sizeof(uint8_t));

    tab_ro_bits = (uint8_t *) malloc(nb * sizeof(uint8_t));
    memset(tab_ro_bits, 0, nb * sizeof(uint8_t));

    tab_rq_registers = (uint16_t *) malloc(nb * sizeof(uint16_t));
    memset(tab_rq_registers, 0, nb * sizeof(uint16_t));

    tab_rw_rq_registers = (uint16_t *) malloc(nb * sizeof(uint16_t));
    memset(tab_rw_rq_registers, 0, nb * sizeof(uint16_t));
}

modbus_t *openConnection() {
	if (settings.mt == mt_TCP) {
		if (options.verbose) 
			std::cerr << "opening tcp connection to " << settings.device_name 
				<< ":" << settings.settings << "\n";
		int port = 1502;
		if (!ctx && strToInt(settings.settings.c_str(), port)) {
    		ctx = modbus_new_tcp(settings.device_name.c_str(), port);
		}
		if (ctx == NULL) {
			std::cerr << "Unable to create the libmodbus context\n";
		}
		else if (modbus_connect(ctx) == -1) {
			std::cerr << "Connection to " << settings.device_name
				<< ":" << settings.settings 
				<< " failed: " << modbus_strerror(errno) << ":" << errno << "\n";
			modbus_free(ctx);
			ctx = 0;
		}
		if (modbus_rtu_get_serial_mode(ctx) != MODBUS_RTU_RS485) {
			std::cerr << "setting RTU 485 mode\n";
			if (modbus_rtu_set_serial_mode(ctx, MODBUS_RTU_RS485) == -1) {
				std::cerr << "modbus_rtu_set_serial_mode: " << modbus_strerror(errno) << "\n";
			}
		}
		else {
			std::cerr << "defaulting to RTU 485 mode\n";
		}
	}
	else if (settings.mt == mt_RTU) {
		int device_id = *settings.devices.begin();
		std::cerr << "opening rtu connection to " << settings.device_name << ", device " << device_id << "\n";
		ctx = modbus_new_rtu(settings.device_name.c_str(), settings.serial.baud, 
			settings.serial.parity, settings.serial.bits, settings.serial.stop_bits);
		if (ctx == NULL) {
			std::cerr << "Unable to create the libmodbus context\n";
		}
		else {
			if (settings.devices.size() > 0) {
				if (modbus_set_slave(ctx, device_id) == -1) {
					std::cerr << "modbus_set_slave: " << modbus_strerror(errno) << "\n";
					modbus_free(ctx);
					ctx = 0;
				}
			}
			if (modbus_connect(ctx) == -1) {
				std::cerr << "Connection failed: " <<  modbus_strerror(errno) << "\n";
				modbus_free(ctx);
				ctx = 0;

			}
		}
	}
	if (!ctx) {
		sendStatus("disconnected");
		return 0;
	}
	/* Save original timeout */
	uint32_t secs, usecs;
	int rc = modbus_get_response_timeout(ctx, &secs, &usecs);
	if (rc == -1) {
		perror("modbus_get_response_timeout");
		modbus_free(ctx);
		ctx = 0;
		sendStatus("disconnected");
	}
	else {
		if (options.verbose) std::cerr << "original response timeout: " << secs 
			<< "." << std::setw(3) << std::setfill('0') << (usecs/1000) << "\n";
		uint64_t new_timeout = options.getTimeout();
		uint32_t new_secs = (new_timeout) ? new_timeout / 1000000 : secs * 2;
		uint32_t new_usecs = (new_timeout) ? new_timeout % 1000000 : usecs * 2;
		while (new_usecs >= 1000000) { new_usecs -= 1000000; new_secs++; }
		rc = modbus_set_response_timeout(ctx, new_secs, new_usecs);
		if (rc == -1) {
			perror("modbus_set_byte_timeout");
			sendStatus("disconnected");
			modbus_free(ctx);
			ctx = 0;
		}
		else {
			if (options.verbose) {
				std::cerr << "new timeout: " << new_secs << "." 
					<< std::setw(3) << std::setfill('0') << (new_usecs/1000) << "\n";
			}
		}
	}
	return ctx;
}

~ModbusClientThread() {
    free(tab_rq_bits);
    free(tab_rp_bits);
    free(tab_ro_bits);
    free(tab_rq_registers);
    free(tab_rw_rq_registers);
	if (ctx) {
	    modbus_close(ctx);
	    modbus_free(ctx);
	}
}

void close_connection() {
	if (options.verbose) std::cerr << " closing connection\n";
	sendStatus("disconnected");
	update_status = true;

	boost::mutex::scoped_lock lock(update_mutex);
	assert(ctx);
	modbus_flush(ctx);
	modbus_close(ctx);
	modbus_free(ctx);
	connected = false;
	ctx = 0;
}

bool check_error(const char *msg, int entry, int *retry) {
	std::cerr << "ERROR: " << msg << entry << " retry: " << *retry << "\n";
	usleep(250);
	if (errno == EAGAIN || errno == EINTR) {
		fprintf(stderr, "%s %s (errno: %d), entry %d retrying %d\n", msg, modbus_strerror(errno), errno, entry, *retry);
		return true;
	}
	else if (errno == EBADF || errno == ECONNRESET || errno == EPIPE) {
		fprintf(stderr, "%s %s (errno: %d), entry %d disconnecting %d\n", msg, modbus_strerror(errno), errno, entry, *retry);
		if (connected) close_connection();
		return false;
	}
	else {
		fprintf(stderr, "%s %s (errno: %d), entry %d reconnecting %d\n", msg, modbus_strerror(errno), errno, entry, *retry);
		if (connected) close_connection();
		return false;
	}
	return true;
}

bool setBit(int addr, bool which) {
	std::cerr << "set bit " << addr << " to " << which << "\n";
	boost::mutex::scoped_lock lock(update_mutex);
	int rc = 0;
	int retries = 3;
	while ( (rc = modbus_write_bit(ctx, addr, (which) ? 1 : 0) ) == -1) {
		perror("modbus_write_bit");
		check_error("modbus_write_bit", addr, &retries);
		if (!connected) return false;
		if (--retries > 0) continue;
		return false;
	}
	return true;
}

bool setRegister(int addr, uint16_t val) {
	if (!connected) return false;
	if (options.verbose) std::cerr << "set register " << addr << " to " << val << "\n";
	boost::mutex::scoped_lock lock(update_mutex);
	int rc = 0;
	int retries = 3;
	if (settings.support_single_register_write)
		rc = modbus_write_register(ctx, addr, val);
	else
		rc = modbus_write_registers(ctx, addr, 1, &val);
	while ( rc == -1) {
		perror("modbus_write_register");
		check_error("modbus_write_register", addr, &retries);
		if (!connected) return false;
		if (--retries > 0) {
			if (settings.support_single_register_write)
				rc = modbus_write_register(ctx, addr, val);
			else
				rc = modbus_write_registers(ctx, addr, 1, &val);
			continue;
		}
		return false;
	}
	return true;
}

bool setRegisters(int addr, uint16_t *val, unsigned int n) {
	if (!connected) return false;
	if (options.verbose) {
		std::cerr << "set register " << addr << " to ";
		for (unsigned int i=0; i<n; ++i) std::cerr << val[i] << " "; 
		std::cerr << "\n";
	}
	boost::mutex::scoped_lock lock(update_mutex);
	int rc = 0;
	int retries = 3;
	rc = modbus_write_registers(ctx, addr, n, val);
	while ( rc == -1) {
		perror("modbus_write_registers");
		check_error("modbus_write_registers", addr, &retries);
		if (!connected) return false;
		if (--retries > 0) {
			usleep(5000);
			rc = modbus_write_registers(ctx, addr, n, val);
			continue;
		}
		return false;
	}
	return true;
}

template<class T>bool collect_selected_updates(BufferMonitor<T> &bm, unsigned int grp, T *dest, 
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
	displayChanges(cmd_interface, changes, dest);
	return true;
}


void operator()() {
	if (iod_cmd_socket_name) {
		cmd_interface = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
		cmd_interface->connect(iod_cmd_socket_name);
	}

	while (!finished) {
		if (!connected) {
			boost::mutex::scoped_lock lock(update_mutex);
			if (ctx) { 
	    		modbus_close(ctx);
	    		modbus_free(ctx);
				ctx = 0;
			}
			if (!ctx) {
				ctx = openConnection();
			}
			if (ctx) {
				connected = true;
				update_status = true;
			}
		}
		else {
			performUpdates();
			if (update_status) {
				sendStatus("initialising");
			}
			if (!collect_selected_updates<uint8_t>(robits_monitor, 1, tab_ro_bits, mc.monitors, "modbus_read_input_bits", modbus_read_input_bits))  {
				std::cerr << "modbus_read_input_bits failed\n";
				//goto modbus_loop_end;
			}
			if (!collect_selected_updates<uint8_t>(bits_monitor, 0, tab_rp_bits, mc.monitors, "modbus_read_bits", modbus_read_bits))  {
				std::cerr << "modbus_read_bits failed\n";
				//goto modbus_loop_end;
			}
			if (!collect_selected_updates<uint16_t>(regs_monitor, 3, tab_rq_registers, mc.monitors, "modbus_read_input registers", modbus_read_input_registers)) { 
				std::cerr << "modbus_read_input_registers failed\n";
				//goto modbus_loop_end;
			}
			if (!collect_selected_updates(holdings_monitor, 4, tab_rw_rq_registers, mc.monitors, "modbus_read_registers", modbus_read_registers)) {
				std::cerr << "modbus_read_registers failed\n";
				//goto modbus_loop_end;
			}
			if ( update_status)  {
				sendStatus("active");
				update_status = false;
			}
		}
		
//modbus_loop_end:
		usleep(100000);
	}
}

};

