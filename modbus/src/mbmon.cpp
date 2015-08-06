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

zmq::context_t *context;
zmq::socket_t *psocket = 0;

struct UserData {
	
};

struct Options {
	bool verbose;
	Options() : verbose(false) {}
};
Options options;

/* Clockwork interface */

/* Send a message using ZMQ */
void sendMessage(zmq::socket_t &socket, const char *message) {
	const char *msg = (message) ? message : "";
	size_t len = strlen(msg);
	zmq::message_t reply (len);
	memcpy ((void *) reply.data (), msg, len);
	while (true) {
		try {
			socket.send (reply);
			break;
		}
		catch (zmq::error_t) {
			if (zmq_errno() == EINTR) continue;
			throw;
		}
	}
   
}

std::list<Value> params;
char *send_command(std::list<Value> &params) {
	if (params.size() == 0) return 0;
	Value cmd_val = params.front();
	params.pop_front();
	std::string cmd = cmd_val.asString();
	char *msg = MessageEncoding::encodeCommand(cmd, &params);
	sendMessage(*psocket, msg);
	size_t size = strlen(msg);
	free(msg);
	zmq::message_t reply;
	if (psocket->recv(&reply)) {
		size = reply.size();
		char *data = (char *)malloc(size+1);
		memcpy(data, reply.data(), size);
		data[size] = 0;

		return data;
	}
	return 0;
}
void process_command(std::list<Value> &params) {
	char * data = send_command(params);
	if (data) {
		std::cout << data << "\n";
		free(data);
	}
}

class ClockworkClientThread {

public:
	ClockworkClientThread(): finished(false) { }
	void operator()() {
	
		zmq::socket_t socket (*context, ZMQ_REQ);
		int linger = 0; // do not wait at socket close time
		socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
		psocket = &socket;

		socket.connect("tcp://localhost:5555");
		
		while (!finished) {
			usleep(500000);	
		}
	}

	bool finished;

};



std::map<int, UserData *> active_addresses;
std::map<int, UserData *> ro_bits;
std::map<int, UserData *> inputs;
//std::map<int, UserData *> registers;

/* Modbus interface */
template<class T> class BufferMonitor {
public:
	std::string name;
	T *last_data;
	T *cmp_data;
	T *dbg_mask;
	size_t buflen;
	int max_read_len; // maximum number of reads into this buffer
	bool initial_read;
	
	BufferMonitor(const char *buffer_name) : name(buffer_name), last_data(0), cmp_data(0), dbg_mask(0), buflen(0), max_read_len(0), initial_read(true) {}
	void check(size_t size, T *upd_data, unsigned int base_address,std::set<ModbusMonitor*> &changes);
};

template<class T>void BufferMonitor<T>::check(size_t size, T *upd_data, unsigned int base_address, std::set<ModbusMonitor*> &changes) {
	if (size != buflen) {
		buflen = size;
		if (last_data) delete[] last_data;
		if (dbg_mask) delete[] dbg_mask;
		if (cmp_data) delete[] cmp_data;
		if (size) {
			initial_read = true;
			last_data = new T[size];
			memset(last_data, 0, size);
			dbg_mask = new T[size];
			memset(dbg_mask, 0xff, size);
			cmp_data = new T[size];
			memset(cmp_data, 0, size);
		}
	}
	if (size) {
		if (initial_read) {
			std::cout << name << " initial read. size: " << size << "\n";
			displayAscii(upd_data, buflen);
			std::cout << "\n";
		}
		T *p = upd_data, *q = cmp_data, *msk = dbg_mask;
		// note: masks are not currently used but we retain this functionality for future
		for (size_t ii=0; ii<size; ++ii) {
			ModbusMonitor *mm;
			if (*q != *p) { 
				mm = ModbusMonitor::lookupAddress(base_address + (q-cmp_data) ); 
				if (mm) changes.insert(mm);
			}
			*q++ = *p++ & *msk++;
		}
		
		// check changes and build a set of changed monitors
		if (!initial_read && memcmp( cmp_data, last_data, size * sizeof(T)) != 0) {
				//std::cout << " "; display(dbg_mask); std::cout << "\n";
				std::cout << "\n->\n"; displayAscii(upd_data, buflen); std::cout << "\n";
		}
		memcpy(last_data, cmp_data, size * sizeof(T));
		initial_read = false;
	}
}

void displayChanges(std::set<ModbusMonitor*> &changes, uint8_t *buffer_addr) {
	if (changes.size()) {
		std::cout << changes.size() << " changes\n";
		std::set<ModbusMonitor*>::iterator iter = changes.begin();
		while (iter != changes.end()) {
			ModbusMonitor *mm = *iter++;
			// note: the monitor address is in the global range grp<<16 + offset
			// this method is only using the addresses in the local range
			mm->set(buffer_addr + ( (mm->address() & 0xffff)) );
		}
	}
}

void displayChanges(std::set<ModbusMonitor*> &changes, uint16_t *buffer_addr) {
	if (changes.size()) {
		std::cout << changes.size() << " changes\n";
		std::set<ModbusMonitor*>::iterator iter = changes.begin();
		while (iter != changes.end()) {
			ModbusMonitor *mm = *iter++;
			// note: the monitor address is in the global range grp<<16 + offset
			// this method is only using the addresses in the local range
			mm->set(buffer_addr + ( (mm->address() & 0xffff)) );
		}
	}
}

class ModbusClientThread{
private:
    modbus_t *ctx;
	boost::mutex update_mutex;

public:
	modbus_t *getContext() {
		update_mutex.lock();
		return ctx;
	}
	void releaseContext() {
		update_mutex.unlock();
	}
    uint8_t *tab_rq_bits;
    uint8_t *tab_rp_bits;
    uint8_t *tab_ro_bits;
    uint16_t *tab_rq_registers;
    uint16_t *tab_rw_rq_registers;

	bool finished;
	bool connected;
	
	std::string host;
	int port;
	
	BufferMonitor<uint8_t> bits_monitor;
	BufferMonitor<uint8_t> robits_monitor;
	BufferMonitor<uint16_t> regs_monitor;
	BufferMonitor<uint16_t> holdings_monitor;

	ModbusClientThread(const char *hostname, int portnum) :  
			ctx(0), tab_rq_bits(0), tab_rp_bits(0), tab_ro_bits(0),
			tab_rq_registers(0), tab_rw_rq_registers(0), 
			finished(false), connected(false), host(hostname), port(portnum),
			bits_monitor("coils"), robits_monitor("discrete"),regs_monitor("registers"),
			holdings_monitor("holdings")
	{
	boost::mutex::scoped_lock(update_mutex);
    ctx = modbus_new_tcp(host.c_str(), port);

	/* Save original timeout */
	unsigned int sec, usec;
	int rc = modbus_get_byte_timeout(ctx, &sec, &usec);
	if (rc == -1) perror("modbus_get_byte_timeout");
	else {
		std::cout << "original timeout: " << sec << "." << std::setw(3) << std::setfill('0') << (usec/1000) << "\n";
		sec *=2;
		usec *= 8; 
		while (usec >= 1000000) { usec -= 1000000; sec++; }
		rc = modbus_set_byte_timeout(ctx, sec, usec);
		if (rc == -1) perror("modbus_set_byte_timeout");
		else std::cout << "new timeout: " << sec << "." << std::setw(3) << std::setfill('0') << (usec/1000) << "\n";

	}
    modbus_set_debug(ctx, FALSE);

    if (modbus_connect(ctx) == -1) {
			fprintf(stderr, "Connection to %s:%d failed: %s (%d)\n",
			host.c_str(), port, 
			modbus_strerror(errno), errno);
        modbus_free(ctx);
		ctx = 0;
		connected = false;
    }
	else connected = true;

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
	boost::mutex::scoped_lock(update_mutex);
	assert(ctx);
	modbus_flush(ctx);
	modbus_close(ctx);
	connected = false;
	ctx = 0;
}

bool check_error(const char *msg, int entry, int *retry) {
	usleep(250000);
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

template<class T>bool collect_updates(BufferMonitor<T> &bm, int grp, T *dest, 
		std::map<int, UserData *> active,
		const char *fn_name,
		int (*read_fn)(modbus_t *ctx, int addr, int nb, T *dest)) {
	std::map<int, UserData*>::iterator iter = active.begin();
	int min =100000;
	int max = 0;
	{
		boost::mutex::scoped_lock(update_mutex);

		while (iter != active.end()) {
			const std::pair<int, UserData*> item = *iter++;
			if (item.first < min) min = item.first;
			if (item.first > max) max = item.first;
/*			
			if (tab_rp_bits[item.first] != tab_rq_bits[item.first]) {
				tab_rq_bits[item.first] = tab_rp_bits[item.first];
				
				std::cout << grp << ":" << (item.first+1) << "->" << (int)tab_rp_bits[item.first] << "\n";
			}
*/
		}
	}
	int rc = -1;
	int retry = 5;
	int offset = min;
	int len = bm.max_read_len;
	if (len == 0) 
		len = max-min+1;
	if (len > 16)
		len = 16;
	if (options.verbose) std::cout << "collecting group " << grp << " start: " << min << " length: " << len << "\n";
	while ( offset <= max) {
		// look for the next active address before sending a request
		int count = 0;
		while (offset <= max && ModbusMonitor::lookupAddress( (grp<<16) + offset) == 0) {
			offset++; ++count;
		}
		if (offset > max) break;
		if (offset+len > max) len = max - offset + 1;
		if (count && options.verbose) {
			std::cout << "skipping " << count << " scanning from address " << offset << "\n";
		}
		while ( (rc = read_fn(ctx, offset, len, dest+offset)) == -1 ) {
			if (rc == -1 && errno == EMBMDATA) { 
				len /= 2; if (len == 0) len = 1; bm.max_read_len = len;
				if (options.verbose) std::cout << "adjusted read size to " << len << " for group " << grp << "\n";
			}
			else if (rc == -1) { 
				check_error(fn_name, offset, &retry); 
				if (!connected) return false;
				continue;
			}
		}
		if (options.verbose) std::cout << "group " << grp << " read range " << offset << " to " << offset+len-1 << "\n";
		offset += len;
	}
	if (!connected) { std::cerr << "Lost connection\n"; return false; }
	std::set<ModbusMonitor*> changes;
	if (min<max) bm.check((max-min+1), dest+min, (grp<<16) + min, changes);
	displayChanges(changes, dest);
	return true;
}

void operator()() {
	int error_count = 0;
	while (!finished) {
		if (!connected) {
			boost::mutex::scoped_lock(update_mutex);
			if (!ctx) ctx = modbus_new_tcp(host.c_str(), port);
		    if (modbus_connect(ctx) == -1) {
				++error_count;
		        fprintf(stderr, "Connection to %s:%d failed: %s (%d)\n",
				host.c_str(), port, 
                modbus_strerror(errno), errno);
		        modbus_free(ctx);
				ctx = 0;
				if (error_count > 5) exit(1);
				usleep(200000);
				continue;
		    }
			else if (ctx) connected = true;
			error_count = 0;
		}

/*
		std::map<int, UserData*>::iterator iter = active_addresses.begin();
		int min =100000;
		int max = 0;
		
		while (iter != active_addresses.end()) {
			boost::mutex::scoped_lock(update_mutex);
			const std::pair<int, UserData*> item = *iter++;
			int rc = -1;
			int retry = 5;
			if (item.first < min) min = item.first;
			if (item.first > max) max = item.first;
			while ( (rc = modbus_read_bits(ctx, item.first, 1, tab_rp_bits+item.first) == -1) ) {
				if (check_error("modbus_read_bits", item.first, &retry)) continue; else break;
			}
			if (!connected) goto modbus_loop_end;
			
			if (tab_rp_bits[item.first] != tab_rq_bits[item.first]) {
				tab_rq_bits[item.first] = tab_rp_bits[item.first];
				
				std::cout << "0:" << (item.first+1) << "->" << (int)tab_rp_bits[item.first] << "\n";
			}
		}
		if (min<max) bits_monitor.check(max-min+1, tab_rp_bits+min);
		if (!connected) goto modbus_loop_end;
*/
		if (!collect_updates(bits_monitor, 0, tab_rp_bits, active_addresses, "modbus_read_bits", modbus_read_bits)) 
			goto modbus_loop_end;
		if (!collect_updates(robits_monitor, 1, tab_ro_bits, ro_bits, "modbus_read_input_bits", modbus_read_input_bits)) 
			goto modbus_loop_end;
		if (!collect_updates(regs_monitor, 3, tab_rq_registers, inputs, "modbus_read_input registers", modbus_read_input_registers)) { 
			std::cout << "group 3 failure\n";
			goto modbus_loop_end;
		}
		if (!collect_updates(holdings_monitor, 4, tab_rw_rq_registers, inputs, "modbus_read_registers", modbus_read_registers)) {
			std::cout << "group 4 failure\n";
			goto modbus_loop_end;
		}
/*
		min =100000;
		max = 0;
		iter = ro_bits.begin();
		while (iter != ro_bits.end()) {
			boost::mutex::scoped_lock(update_mutex);
			const std::pair<int, UserData*> item = *iter++;
			int rc = -1;
			int retry = 5;
			uint8_t res; 
			assert(ctx != 0);
			if (item.first < min) min = item.first;
			if (item.first > max) max = item.first;
			while ( ( rc = modbus_read_input_bits(ctx, item.first, 1, &res)  == -1) ) {
				if (check_error("modbus_read_input_bits", item.first, &retry)) continue; else break;
			}
			if (!connected) goto modbus_loop_end;
			if (res != tab_ro_bits[item.first]) {
				tab_ro_bits[item.first] = res;
				std::cout << "1:" << (item.first+1) << "->" << (int)tab_ro_bits[item.first] << "\n";				
			}
		}
		if (min<max)
			robits_monitor.check(max-min+1, tab_ro_bits+min);
	{
		std::map<int, UserData*>::iterator iter = inputs.begin();
		int min =100000;
		int max = 0;
		iter = inputs.begin();
		while (iter != inputs.end()) {
			boost::mutex::scoped_lock(update_mutex);
			const std::pair<int, UserData*> item = *iter;
			iter++;
			int rc = -1;
			int retry = 5;
			uint16_t res; 
			if (item.first < min) min = item.first;
			if (item.first > max) max = item.first;
			while ( ( rc = modbus_read_registers(ctx, item.first, 1, &res)  == -1) ) {
				if (check_error("modbus_read_registers", item.first, &retry)) continue; else break;
			}
			if (!connected) goto modbus_loop_end;
			if (res != tab_rw_rq_registers[item.first]) {
				tab_rw_rq_registers[item.first] = res;
				std::cout << "4:" << (item.first+1) << "->" << tab_rw_rq_registers[item.first] << "\n";
			}
		}
		if (min<max) {
			std::set<ModbusMonitor*>changes;
			regs_monitor.check((max-min+1)*2, (uint8_t*)(tab_rw_rq_registers+min), min, changes);
		}

		min =100000;
		max = 0;
		iter = inputs.begin();
		while (iter != inputs.end()) {
			boost::mutex::scoped_lock(update_mutex);
			const std::pair<int, UserData*> item = *iter++;
			int rc = -1;
			int retry = 5;
			uint16_t res; 
			if (item.first < min) min = item.first;
			if (item.first > max) max = item.first;
			while ( ( rc = modbus_read_registers(ctx, item.first, 1, &res)  == -1) ) {
				if (check_error("modbus_read_registers", item.first, &retry)) continue; else break;
			}
			if (!connected) goto modbus_loop_end;
			if (res != tab_rq_registers[item.first]) {
				tab_rq_registers[item.first] = res;
				std::cout << "3:" << (item.first+1) << "->" << tab_rq_registers[item.first] << "\n";
			}
		}
		if (min<max) {
			std::set<ModbusMonitor*> changes;
			regs_monitor.check((max-min+1)*2, (uint8_t*)(tab_rw_rq_registers+min), min, changes);
		}
	}
*/
		
modbus_loop_end:	
		usleep(100000);
	}
}

};

ModbusClientThread *mb = 0;

void usage(const char *prog) {
	std::cout << prog << " [-h hostname] [ -p port]\n\n"
		<< "defaults to -h localhost -p 1502\n"; 
}

using namespace std;
int main(int argc, char *argv[]) {
	context = new zmq::context_t;
	
	const char *hostname = "127.0.0.1"; //"10.1.1.3";
	int portnum = 1502; //502;
	const char *config_filename = "modbus_mappings.txt";
	
	int arg = 1;
	while (arg<argc) {
		if ( strcmp(argv[arg], "-h") == 0 && arg+1 < argc) hostname = argv[++arg];
		else if ( strcmp(argv[arg], "-p") == 0 && arg+1 < argc) {
			char *q;
			long p = strtol(argv[++arg], &q,10);
			if (q != argv[arg]) portnum = (int)p;
		}
		else if ( strcmp(argv[arg], "-c") == 0 && arg+1 < argc) {
			config_filename = argv[++arg];
		}
		else if ( strcmp(argv[arg], "-v") == 0) {
			options.verbose = true;
		}
		else if (argv[arg][0] == '-'){ usage(argv[0]); exit(0); }
		else break;
		++arg;
	}

	PLCInterface plc;
	if (!plc.load("koyo.conf")) {
		cerr << "Failed to load plc mapping configuration\n";
		exit(1);
	}
	
	MonitorConfiguration mc;
	if (!mc.load(config_filename)) {
		cerr << "Failed to load modbus mappings to be monitored\n";
		exit(1);
	}
	
	//for (int i=0; i<10; ++i) { active_addresses[i] = 0; ro_bits[i] = 0; }
	std::map<std::string, ModbusMonitor>::const_iterator iter = mc.monitors.begin();
	while (iter != mc.monitors.end()) {
		const std::pair<std::string, ModbusMonitor> &item = *iter++;
		if (item.second.group() == 0) {
			for (int i=0; i<item.second.length(); ++i) {
				if (options.verbose) 
					std::cout << "monitoring: " << item.second.group() << ":" 
					<< (item.second.address()+i) <<" " << item.second.name() << "\n";
				active_addresses[item.second.address()+i] = 0;
			}
		}
		else if (item.second.group() == 1) {
			for (int i=0; i<item.second.length(); ++i) {
				if (options.verbose)
					std::cout << "monitoring: " << item.second.group()
					 << ":" << (item.second.address()+i) <<" " << item.second.name() << "\n";
				ro_bits[item.second.address()+i] = 0;
			}
		}
		else if (item.second.group() >= 3) {
			for (int i=0; i<item.second.length(); ++i) {
				if (options.verbose) 
					std::cout << "monitoring: " << item.second.group() << ":" 
					<< (item.second.address()+i) <<" " << item.second.name() << "\n";
				inputs[item.second.address()+i] = 0;
			}
		}
	}

	ModbusClientThread modbus_interface(hostname, portnum);
	mb = &modbus_interface;
	boost::thread monitor_modbus(boost::ref(modbus_interface));
	
	while (true) {
		usleep(100000);
	}
}