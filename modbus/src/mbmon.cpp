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
#include "cJSON.h"

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
	if (options.verbose) std::cout << " sending: " << msg << "\n";
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
		if (options.verbose) std::cout << data << "\n";
		free(data);
	}
}

class ClockworkClientThread {

public:
	ClockworkClientThread(): finished(false) { 
		int linger = 0; // do not wait at socket close time

		socket = new zmq::socket_t(*context, ZMQ_REQ);
		socket->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
		psocket = socket;
		socket->connect("tcp://localhost:5555");
	}
	void operator()() {
		while (!finished) {
			usleep(500000);	
		}
	}
	zmq::socket_t *socket;
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
	void setMaskBits(int start, int num);
};

template <class T>void BufferMonitor<T>::setMaskBits(int start, int num) {
	if (!dbg_mask) return;
	//std::cout << "masking: " << start << " to " << (start+num-1) << "\n";
	int i = start;
	while (i<start+num && i+start < buflen) {
		dbg_mask[i] = 0xff;
	}
}


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
		if (options.verbose && initial_read) {
			//std::cout << name << " initial read. size: " << size << "\n";
			displayAscii(upd_data, buflen);
			std::cout << "\n";
		}
		T *p = upd_data, *q = cmp_data; //, *msk = dbg_mask;
		// note: masks are not currently used but we retain this functionality for future
		for (size_t ii=0; ii<size; ++ii) {
			ModbusMonitor *mm;
			if (*q != *p) { 
				if (options.verbose) std::cout << "change at " << (base_address + (q-cmp_data) ) << "\n";
				mm = ModbusMonitor::lookupAddress(base_address + (q-cmp_data) ); 
				if (mm) { changes.insert(mm); if (options.verbose) std::cout << "found change " << mm->name() << "\n"; }
			}
			*q++ = *p++; // & *msk++;
		}
		
		// check changes and build a set of changed monitors
		if (!initial_read && memcmp( cmp_data, last_data, size * sizeof(T)) != 0) {
				
				if (options.verbose) { 
					std::cout << "\n"; display(upd_data, buflen); std::cout << "\n";
					//std::cout << " "; display(dbg_mask, buflen); std::cout << "\n";
					}
		}
		memcpy(last_data, cmp_data, size * sizeof(T));
		initial_read = false;
	}
}

void displayChanges(std::set<ModbusMonitor*> &changes, uint8_t *buffer_addr) {
	if (changes.size()) {
		if (options.verbose) std::cout << changes.size() << " changes\n";
		std::set<ModbusMonitor*>::iterator iter = changes.begin();
		while (iter != changes.end()) {
			ModbusMonitor *mm = *iter++;
			// note: the monitor address is in the global range grp<<16 + offset
			// this method is only using the addresses in the local range
			uint8_t *val = buffer_addr + ( (mm->address() & 0xffff));
			std::cout << mm->name() << " "; mm->set( val );
			
			if ( (mm->group() == 0 || mm->group() == 1) && mm->length()==1) {
				std::list<Value> cmd;
				cmd.push_back("SET");
				cmd.push_back(mm->name().c_str());
				cmd.push_back("TO");
				if (*val) cmd.push_back("on"); else cmd.push_back("off");
				process_command(cmd);
			}
		}
	}
}

void displayChanges(std::set<ModbusMonitor*> &changes, uint16_t *buffer_addr) {
	if (changes.size()) {
		if (options.verbose) std::cout << changes.size() << " changes\n";
		std::set<ModbusMonitor*>::iterator iter = changes.begin();
		while (iter != changes.end()) {
			ModbusMonitor *mm = *iter++;
			// note: the monitor address is in the global range grp<<16 + offset
			// this method is only using the addresses in the local range
			uint16_t *val = buffer_addr + ( (mm->address() & 0xffff));
			std::cout << mm->name() << " "; mm->set( val );
			//mm->set(buffer_addr + ( (mm->address() & 0xffff)) );
			if (mm->length()==1) {
				std::list<Value> cmd;
				cmd.push_back("PROPERTY");
				char buf[100];
				snprintf(buf, 100, "%s", mm->name().c_str());
				char *p = strrchr(buf, '.');
				if (p) {
					*p++ = 0;
					cmd.push_back(buf);
					cmd.push_back(p);
				}
				else {
					cmd.push_back(buf);
					cmd.push_back("VALUE");
				}
				cmd.push_back(*val);
				process_command(cmd);
			}
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
		if (options.verbose) std::cout << "original timeout: " << sec << "." << std::setw(3) << std::setfill('0') << (usec/1000) << "\n";
		sec *=2;
		usec *= 8; 
		while (usec >= 1000000) { usec -= 1000000; sec++; }
		rc = modbus_set_byte_timeout(ctx, sec, usec);
		if (rc == -1) perror("modbus_set_byte_timeout");
		else 
			if (options.verbose) 
				std::cout << "new timeout: " << sec << "." << std::setw(3) << std::setfill('0') << (usec/1000) << "\n";

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
		}
	}
	if (min>max) return true; // nothing active in this group
	int rc = -1;
	int retry = 5;
	int offset = min;
	int len = bm.max_read_len;
	if (len == 0) 
		len = max-min+1;
	//if (len > 16)
	//	len = 16;
	//if (options.verbose) std::cout << "collecting group " << grp << " start: " << min << " length: " << len << "\n";
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
		//if (options.verbose) std::cout << "group " << grp << " read range " << offset << " to " << offset+len-1 << "\n";
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
		else {
			if (!collect_updates(bits_monitor, 0, tab_rp_bits, active_addresses, "modbus_read_bits", modbus_read_bits)) 
				goto modbus_loop_end;
			if (!collect_updates(robits_monitor, 1, tab_ro_bits, ro_bits, "modbus_read_input_bits", modbus_read_input_bits)) 
				goto modbus_loop_end;
			if (!collect_updates(regs_monitor, 3, tab_rq_registers, inputs, "modbus_read_input registers", modbus_read_input_registers)) { 
				goto modbus_loop_end;
			}
			/*if (!collect_updates(holdings_monitor, 4, tab_rw_rq_registers, inputs, "modbus_read_registers", modbus_read_registers)) {
				goto modbus_loop_end;
			}*/
		}
		
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

	const char *hostname = "127.0.0.1"; //"10.1.1.3";
	int portnum = 1502; //502;
	const char *config_filename = "modbus_mappings.txt";
	const char *channel_name = "PLC_MONITOR";

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
		else if ( strcmp(argv[arg], "--channel") == 0 && arg+1 < argc) {
			channel_name = argv[++arg];
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
		std::cerr << "Failed to load plc mapping configuration\n";
		exit(1);
	}

	context = new zmq::context_t;

	ClockworkClientThread cw_client;
	boost::thread monitor_cw(boost::ref(cw_client));

	MonitorConfiguration mc;
	/*
	if (!mc.load(config_filename)) {
		cerr << "Failed to load modbus mappings to be monitored\n";
		exit(1);
	}
	*/
	{
	std::list<Value>cmd;
	cmd.push_back("CHANNEL");
	cmd.push_back(channel_name);
	char *response = send_command(cmd);
	if (options.verbose) std::cout << response << "\n";
	cJSON *obj = cJSON_Parse(response);
	free(response);
	
	std::string chn_instance_name;
	if (obj) {
		cJSON *name_js = cJSON_GetObjectItem(obj, "name");
		chn_instance_name = name_js->valuestring;
		cJSON_Delete(obj);
		obj = 0;
	}
	std::cout << chn_instance_name << "\n";
	
	cmd.clear();
	cmd.push_back("REFRESH");
	cmd.push_back(chn_instance_name.c_str());
	response = send_command(cmd);
	if (options.verbose) std::cout << response << "\n";
	obj = cJSON_Parse(response);
	
	
	if (obj) {
		if (obj->type != cJSON_Array) {
			std::cout << "error. clock response is not an array";
			char *item = cJSON_Print(obj);
			std::cout << item << "\n";
		}
		cJSON *item = obj->child;
		while (item) {
			cJSON *name_js = cJSON_GetObjectItem(item, "name");
			cJSON *addr_js = cJSON_GetObjectItem(item, "address");
			cJSON *length_js = cJSON_GetObjectItem(item, "length");
			cJSON *type_js = cJSON_GetObjectItem(item, "type");
			
			std::string name;
			if (name_js && name_js->type == cJSON_String) {
				name = name_js->valuestring;
			}
			int length = length_js->valueint;
			std::string type;
			if (type_js && type_js->type == cJSON_String) {
				type = type_js->valuestring;
			}
			int group = 1;
			if (type == "INPUTBIT") group = 1;
			else if (type == "OUTPUTBIT") group = 0;
			else if (type == "INPUTREGISTER") group = 3;
			else if (type == "OUTPUTREGISTER") group = 4;

			int addr = 0;
			std::string addr_str;
			if (addr_js && addr_js->type == cJSON_String) {
				addr_str = addr_js->valuestring;
				std::pair<int, int> plc_addr = plc.decode(addr_str.c_str());
				addr = plc_addr.second;
			}
			ModbusMonitor *mm = new ModbusMonitor(name, group, addr, length);
			mc.monitors.insert(std::make_pair(name, *mm) );

			mm->add();
			item = item->next;
		}

	}
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
