#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <modbus.h>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Value_Input.H>
#include <boost/thread.hpp>
#include "core_loader.h"
#include <zmq.hpp>
#include <value.h>
#include <MessageEncoding.h>
#include <plc_interface.h>

zmq::context_t *context;
zmq::socket_t *psocket = 0;
PLCInterface *plc;

const char *V_CoreKeepAlive = "TA7";
const char *V_CoreScalesLiveWeight = "V4020";
const char *V_CoreScalesCapturedWeight = "V4022";
const char *F_CoreScalesSteady = "C411";
const char *I_CoreLoaderBalePresent = "X5";
const char *I_CoreLoaderAtPos = "X6";
const char *O_CoreLoaderUpBlock = "Y50";
const char *O_CoreLoaderUp = "Y6";

const char *I_GrabBalePresent = "X4";
const char *I_CutterBalePresent = "X5";
const char *I_FeederBalePresent = "X4";
const char *I_InsertBalePresent = "X15"; 
const char *O_InsertForward = "Y0";

const char *I_GrabBaleAtBaleGate = "X21";
const char *O_GrabBaleGateUp = "Y17";
const char *O_GrabBaleGateBlock = "Y30";
const char *V_GrabBaleGateFolioSize = "V12002";
const char *V_GrabBaleGateBaleNo = "V12003";


struct UpdateMessage {
	int address;
	Fl_Widget *widget;
	int value;
	UpdateMessage(int a, Fl_Widget *w, int v) : address(a), widget(w), value(v) {}
};

std::map<int, Fl_Widget *> active_addresses;
std::map<int, Fl_Widget *> ro_bits;
std::map<int, Fl_Widget *> inputs;

bool auto_mode = false;

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

		socket.connect("tcp://localhost:10001");
		
		while (!finished) {
			usleep(500000);	
		}
	}

	bool finished;

};

/* Modbus interface */

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
    uint16_t *tab_rp_registers;

	bool finished;
	bool connected;
	
	std::string host;
	int port;

	ModbusClientThread(const char *hostname, int portnum) : ctx(0), tab_rq_bits(0), tab_rp_bits(0), tab_ro_bits(0),
			tab_rq_registers(0), tab_rw_rq_registers(0), 
			tab_rp_registers(0), finished(false), connected(false), host(hostname), port(portnum) {
	boost::mutex::scoped_lock(update_mutex);
    ctx = modbus_new_tcp(host.c_str(), port);

	/* Save original timeout */
	unsigned int sec, usec;
	int rc = modbus_get_byte_timeout(ctx, &sec, &usec);
	if (rc == -1) perror("modbus_get_byte_timeout");
	else {
		std::cout << "original timeout: " << sec << "." << std::setw(3) << std::setfill('0') << (usec/1000) << "\n";
		usec *= 2; 
		if (usec >= 1000000) { usec -= 1000000; sec++; }
		rc = modbus_set_byte_timeout(ctx, sec, usec);
		if (rc == -1) perror("modbus_set_byte_timeout");
	}
    modbus_set_debug(ctx, FALSE);

    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s (%d)\n",
                modbus_strerror(errno), errno);
        modbus_free(ctx);
		ctx = 0;
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

    tab_rp_registers = (uint16_t *) malloc(nb * sizeof(uint16_t));
    memset(tab_rp_registers, 0, nb * sizeof(uint16_t));

    tab_rw_rq_registers = (uint16_t *) malloc(nb * sizeof(uint16_t));
    memset(tab_rw_rq_registers, 0, nb * sizeof(uint16_t));
}

~ModbusClientThread() {
    free(tab_rq_bits);
    free(tab_rp_bits);
    free(tab_ro_bits);
    free(tab_rq_registers);
    free(tab_rp_registers);
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
	if (errno == EAGAIN || errno == EINTR) {
		fprintf(stderr, "%s %s (%d), entry %d retrying %d\n", msg, modbus_strerror(errno), errno, entry, *retry);
		usleep(100);
		return true;
	}
	else if (errno == EBADF || errno == ECONNRESET || errno == EPIPE) {
		fprintf(stderr, "%s %s (%d), entry %d disconnecting %d\n", msg, modbus_strerror(errno), errno, entry, *retry);
		close_connection();
	}
	else {
		fprintf(stderr, "%s %s (%d), entry %d disconnecting %d\n", msg, modbus_strerror(errno), errno, entry, *retry);
		close_connection();
		return false;
		usleep(1000);
	}
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
		        fprintf(stderr, "Connection failed: %s (%d)\n",
                modbus_strerror(errno), errno);
		        modbus_free(ctx);
				ctx = 0;
				if (error_count > 5) exit(1);
				usleep(200000);
				continue;
		    }
			else connected = true;
			error_count = 0;
		}
		std::map<int, Fl_Widget*>::iterator iter = active_addresses.begin();
		while (iter != active_addresses.end()) {
			boost::mutex::scoped_lock(update_mutex);
			const std::pair<int, Fl_Widget*> item = *iter++;
			int rc = -1;
			int retry = 5;
			while ( (rc = modbus_read_bits(ctx, item.first, 1, tab_rp_bits+item.first) == -1) ) {
				if (check_error("modbus_read_bits", item.first, &retry)) continue; else break;
			}
			if (!connected) goto modbus_loop_end;
			
			if (tab_rp_bits[item.first] != tab_rq_bits[item.first]) {
				tab_rq_bits[item.first] = tab_rp_bits[item.first];
				
				Fl::lock();
				
				Fl::awake((void *)new UpdateMessage(item.first, item.second, tab_rp_bits[item.first]) );
				Fl::unlock();
				
			}
		}
		if (!connected) goto modbus_loop_end;
		iter = ro_bits.begin();
		while (iter != ro_bits.end()) {
			boost::mutex::scoped_lock(update_mutex);
			const std::pair<int, Fl_Widget*> item = *iter++;
			int rc = -1;
			int retry = 5;
			uint8_t res; 
			assert(ctx != 0);
			while ( ( rc = modbus_read_input_bits(ctx, item.first, 1, &res)  == -1) ) {
				if (check_error("modbus_read_input_bits", item.first, &retry)) continue; else break;
			}
			if (!connected) goto modbus_loop_end;
			if (res != tab_ro_bits[item.first]) {
				tab_ro_bits[item.first] = res;
				
				Fl::lock();
				
				Fl::awake((void *)new UpdateMessage(item.first, item.second, res) );
				Fl::unlock();
				
			}
		}
		iter = inputs.begin();
		while (iter != inputs.end()) {
			boost::mutex::scoped_lock(update_mutex);
			const std::pair<int, Fl_Widget*> item = *iter++;
			int rc = -1;
			int retry = 5;
			uint16_t res; 
			while ( ( rc = modbus_read_registers(ctx, item.first, 1, &res)  == -1) ) {
				if (check_error("modbus_read_registers", item.first, &retry)) continue; else break;
			}
			if (!connected) goto modbus_loop_end;
			if (res != tab_rw_rq_registers[item.first]) {
				tab_rw_rq_registers[item.first] = res;
				
				Fl::lock();
				
				Fl::awake((void *)new UpdateMessage(item.first, item.second, res) );
				Fl::unlock();
				
			}
		}

		
modbus_loop_end:	
		usleep(100000);
	}
}

};

ModbusClientThread *mb = 0;


void press(Fl_Widget *w, void *data) {
	std::pair<int, int>mbadr = plc->decode( (const char *)data );
	//int group = mbadr.first;
	int addr= mbadr.second;
	printf("pressed btn with address %d\n", addr);
	Fl_Check_Button *btn = dynamic_cast<Fl_Check_Button*>(w);
	if (btn) {
		int val = btn->value();
	    printf("Address = %d, value = %d\n", addr, val);
		mb->tab_rq_bits[addr] = val;
	}
	else {
		Fl_Light_Button *btn = dynamic_cast<Fl_Light_Button*>(w);
		if (btn) {
			int val = btn->value();
		    printf("Address = %d, value = %d\n", addr, val);
			mb->tab_rq_bits[addr] = val;
		}
		else
			mb->tab_rq_bits[addr] = 1;
	}
	{
		modbus_t *ctx = mb->getContext();
		int rc = 0;
	    while ( (rc = modbus_write_bit(ctx, addr, mb->tab_rq_bits[addr]) ) == -1) {
	        printf("Address = %d, value = %d\n", addr, mb->tab_rq_bits[addr]);
			fprintf(stderr, "%s %s (%d), entry\n", "modbus_write_bit", modbus_strerror(errno), errno);
			if (errno == EAGAIN || errno == EINTR) continue; else break;
		}
	    if (rc != 1) {
	        printf("ERROR modbus_write_bit (%d)\n", rc);
	        printf("Address = %d, value = %d\n", addr, mb->tab_rq_bits[addr]);
	    }
		mb->releaseContext();
	} 
}

void press(Fl_Light_Button*w, void*v) {
	press((Fl_Widget *)w, v);
}

void set_auto_mode(Fl_Light_Button*w, void*v) {
	Fl_Light_Button *btn = dynamic_cast<Fl_Light_Button*>(w);
	auto_mode = btn->value();
}

void save(Fl_Value_Input*in, void*data) {
	std::pair<int, int>mbadr = plc->decode( (const char *)data );
	//int group = mbadr.first;
	int addr= mbadr.second;
	{
		modbus_t *ctx = mb->getContext();
		double v = in->value();
		std::cout << "reg: " << addr << " has value " << v  << "\n";
		long val = v + 0.5;
		mb->tab_rw_rq_registers[addr] = val;
	    int rc = modbus_write_register(ctx, addr, mb->tab_rw_rq_registers[addr]);
	    if (rc != 1) {
	        printf("ERROR modbus_write_register (%d)\n", rc);
	        printf("Address = %d, value = %d\n", addr, mb->tab_rw_rq_registers[addr]);
	    } 
		mb->releaseContext();
	}
}

void init(const char * mbaddr, Fl_Value_Input*w) {
	{
		std::pair<int, int>mbadr = plc->decode( (const char *)mbaddr );
		//int group = mbadr.first;
		int addr= mbadr.second;
		modbus_t *ctx = mb->getContext();
		inputs[addr] = w;
		int rc = modbus_read_registers(ctx, addr, 1, mb->tab_rp_registers+addr);
		if (rc == -1) {
			fprintf(stderr, "%s\n", modbus_strerror(errno));
		}
		if (mb->tab_rp_registers[addr] != mb->tab_rw_rq_registers[addr]) 
			mb->tab_rw_rq_registers[addr] = mb->tab_rp_registers[addr];
		std::cout << "reg 4:" << addr << " is " << mb->tab_rw_rq_registers[addr] << "\n";
		w->value(mb->tab_rw_rq_registers[addr]);
		mb->releaseContext();
	}
}

void init(const char * mbaddr, Fl_Widget *w, bool is_input) {
	{
		std::pair<int, int>mbadr = plc->decode( (const char *)mbaddr );
		//int group = mbadr.first;
		int addr= mbadr.second;
		modbus_t *ctx = mb->getContext();
		int rc = 0;
		if (is_input) {
			active_addresses[addr] = w;
			rc = modbus_read_bits(ctx, addr, 1, mb->tab_rp_bits+addr);
		}
		else {
			ro_bits[addr] = w;
			rc = modbus_read_input_bits(ctx, addr, 1, mb->tab_ro_bits+addr);
		}
		if (rc == -1) {
			fprintf(stderr, "%s\n", modbus_strerror(errno));
		}
		if (is_input)
			std::cout << "bit 0:" << addr << " is " << (int)mb->tab_rp_bits[addr] << "\n";
		else
			std::cout << "bit 1:" << addr << " is " << (int)mb->tab_ro_bits[addr] << "\n";
		Fl_Light_Button *btn = dynamic_cast<Fl_Light_Button*>(w);
		if (btn) btn->value(mb->tab_rp_bits[addr]);
		Fl_Box *box = dynamic_cast<Fl_Box*>(w);
		if (box) { if (mb->tab_ro_bits[addr]) box->activate(); else box->deactivate(); }
		mb->releaseContext();
	}
}


int main(int argc, char **argv) {
	context = new zmq::context_t;
	plc = new PLCInterface;
	plc->load("koyo.conf");
	
	//const char *hostname = "10.1.1.3";
	//int portnum = 502;
	const char *hostname = "127.0.0.1";
	int portnum = 1502;
	
	int arg = 1;
	while (arg<argc) {
		if ( strcmp(argv[arg], "-h") == 0 && arg+1 < argc) hostname = argv[++arg];
		else if ( strcmp(argv[arg], "-p") == 0 && arg+1 < argc) {
			char *q;
			long p = strtol(argv[++arg], &q,10);
			if (q != argv[arg]) portnum = (int)p;
		}
		++arg;
	}

	CoreLoader gui;
	
	ModbusClientThread modbus_interface(hostname, portnum);
	mb = &modbus_interface;
	boost::thread monitor_modbus(boost::ref(modbus_interface));

	usleep(1000);
	
	int V_CoreKeepAliveAddr = 0;
	{
		std::pair<int, int>res = plc->decode(V_CoreKeepAlive);
		V_CoreKeepAliveAddr = res.second;
	}

	unsigned int keep_alive = 0;
	{
		modbus_t *ctx = mb->getContext();
		modbus_write_register(ctx, V_CoreKeepAliveAddr, keep_alive);
		mb->releaseContext();
	}

	Fl_Window *window  = gui.make_window();
	window->show(1, argv);
	Fl::lock();

	
	ClockworkClientThread cw_client;
	boost::thread monitor_cw(boost::ref(cw_client));

	while (Fl::wait() > 0) {
		if (void *msg = Fl::thread_message()) {
			std::cerr << " got message\n";
			UpdateMessage *um = (UpdateMessage*)(msg);
			std::cout << um->address << "\n";
			{
				Fl_Light_Button *btn = dynamic_cast<Fl_Light_Button*>(um->widget);
				if (btn) btn->value(um->value);
				else {
					Fl_Box *box = dynamic_cast<Fl_Box*>(um->widget);
					if (box) { if (um->value) box->activate(); else box->deactivate();}
					else {
						Fl_Value_Input *in = dynamic_cast<Fl_Value_Input*>(um->widget);
						if (in) {
							in->value(um->value);
						}
					}
				}
			}
			delete um;
		//  char buf[10];
		//  sprintf(buf, "%d", modbus_interface.tab_rw_rq_registers[1074]);
		//  screen_num->value(buf);
		}
		keep_alive++;
		{
			modbus_t *ctx = mb->getContext();
			modbus_write_register(ctx, V_CoreKeepAliveAddr, keep_alive);
			mb->releaseContext();
		}
		usleep(5000);
	}

	return Fl::run();
}

