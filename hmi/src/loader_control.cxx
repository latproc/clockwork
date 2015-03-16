#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <modbus.h>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Check_Button.H>
#include <boost/thread.hpp>
#include "loader_panel.h"
#include <zmq.hpp>
#include <value.h>
#include <MessageEncoding.h>
#include "loader_config.h"

zmq::context_t *context;
zmq::socket_t *psocket = 0;

struct UpdateMessage {
	int address;
	Fl_Widget *widget;
	int value;
	UpdateMessage(int a, Fl_Widget *w, int v) : address(a), widget(w), value(v) {}
};

std::map<int, Fl_Widget *> active_addresses;
std::map<int, Fl_Widget *> ro_bits;

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

class ModbusClientThread{

public:
    modbus_t *ctx;
    uint8_t *tab_rq_bits;
    uint8_t *tab_rp_bits;
    uint8_t *tab_ro_bits;
    uint16_t *tab_rq_registers;
    uint16_t *tab_rw_rq_registers;
    uint16_t *tab_rp_registers;

	bool finished;

	ModbusClientThread() : ctx(0), tab_rq_bits(0), tab_rp_bits(0), tab_ro_bits(0),
			tab_rq_registers(0), tab_rw_rq_registers(0), 
			tab_rp_registers(0), finished(false) {
    ctx = modbus_new_tcp("127.0.0.1", 1502);
    modbus_set_debug(ctx, FALSE);

    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n",
                modbus_strerror(errno));
        modbus_free(ctx);
		ctx = 0;
    }

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

    modbus_close(ctx);
    modbus_free(ctx);
}

void operator()() {
	while (!finished) {
		std::map<int, Fl_Widget*>::iterator iter = active_addresses.begin();
		while (iter != active_addresses.end()) {
			const std::pair<int, Fl_Widget*> item = *iter++;
			int rc = modbus_read_bits(ctx, item.first, 1, tab_rp_bits+item.first);
			if (rc == -1) {
				fprintf(stderr, "%s\n", modbus_strerror(errno));
			}
			if (tab_rp_bits[item.first] != tab_rq_bits[item.first]) {
				tab_rq_bits[item.first] = tab_rp_bits[item.first];
				
				Fl::lock();
				
				Fl::awake((void *)new UpdateMessage(item.first, item.second, tab_rp_bits[item.first]) );
				Fl::unlock();
				
			}
		}
		iter = ro_bits.begin();
		while (iter != ro_bits.end()) {
			const std::pair<int, Fl_Widget*> item = *iter++;
			uint8_t res;
			int rc = modbus_read_input_bits(ctx, item.first, 1, &res);
			if (rc == -1) {
				fprintf(stderr, "%s\n", modbus_strerror(errno));
			}
			if (res != tab_ro_bits[item.first]) {
				tab_ro_bits[item.first] = res;
				
				Fl::lock();
				
				Fl::awake((void *)new UpdateMessage(item.first, item.second, res) );
				Fl::unlock();
				
			}
		}
		
		
		usleep(100000);
	}
}

};

ModbusClientThread *mb = 0;


void press(Fl_Widget *w, void *data) {
	int *addr= (int*)data;
	Fl_Check_Button *btn = dynamic_cast<Fl_Check_Button*>(w);
	if (btn) {
		int val = btn->value();
		mb->tab_rq_bits[*addr] = val;
	}
	else {
		Fl_Light_Button *btn = dynamic_cast<Fl_Light_Button*>(w);
		if (btn) {
			int val = btn->value();
			mb->tab_rq_bits[*addr] = val;
		}
		else
			mb->tab_rq_bits[*addr] = 1;
	}
    int rc = modbus_write_bit(mb->ctx, *addr, mb->tab_rq_bits[*addr]);
    if (rc != 1) {
        printf("ERROR modbus_write_bit (%d)\n", rc);
        printf("Address = %d, value = %d\n", *addr, mb->tab_rq_bits[0]);
    } 
}

void press(Fl_Light_Button*w, void*v) {
	press((Fl_Widget *)w, v);
}

void init(int addr, Fl_Widget *w, bool is_input) {
	int rc = 0;
	if (is_input) {
		active_addresses[addr] = w;
		rc = modbus_read_bits(mb->ctx, addr, 1, mb->tab_rp_bits+addr);
	}
	else {
		ro_bits[addr] = w;
		rc = modbus_read_input_bits(mb->ctx, addr, 1, mb->tab_ro_bits+addr);
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
}


void start(Fl_Widget *w, void *data) {
	int addr=1004; /* start */
	mb->tab_rq_bits[addr] = 1;
    int rc = modbus_write_bit(mb->ctx, addr, mb->tab_rq_bits[addr]);
    if (rc != 1) {
        printf("ERROR modbus_write_bit (%d)\n", rc);
        printf("Address = %d, value = %d\n", addr, mb->tab_rq_bits[0]);
    } 
}

void stop(Fl_Widget *w, void *data) {
	int addr=1005; /* stop */
	mb->tab_rq_bits[addr] = 1;
    int rc = modbus_write_bit(mb->ctx, addr, mb->tab_rq_bits[addr]);
    if (rc != 1) {
        printf("ERROR modbus_write_bit (%d)\n", rc);
        printf("Address = %d, value = %d\n", addr, mb->tab_rq_bits[0]);
    } 
}

void change_screen(Fl_Widget *w, void *data) {
	Fl_Output *screen_num = (Fl_Output*)data;
	Fl_Input *in = (Fl_Input*)w;
	const char *val = in->value();
	long scr = 0;
	char *endp = 0;
	scr = strtol(val, &endp, 10);
	if (endp != val ) {
		mb->tab_rw_rq_registers[1074] = (uint16_t)scr;
		char buf[10];
		sprintf(buf, "%d", mb->tab_rw_rq_registers[1074]);
		screen_num->value(buf);
		int rc = modbus_write_register(mb->ctx, 1074, mb->tab_rw_rq_registers[1074]);
		if (rc == -1) {
			fprintf(stderr, "%s\n", modbus_strerror(errno));
		}
	}
}


int main(int argc, char **argv) {
	context = new zmq::context_t;

	
	LoaderPanel gui;
	
#if 0
  Fl_Window *window = new Fl_Window(340,360);
  Fl_Box *box = new Fl_Box(20,40,300,60,"modbus.lpc");
  box->box(FL_NO_BOX);
  box->labelfont(FL_BOLD+FL_ITALIC);
  box->labelsize(24);

  Fl_Button *start_btn= new Fl_Button(20, 140, 130, 50, "start");
  start_btn->labelsize(36);
  start_btn->callback(start, 0);
  Fl_Button *stop_btn= new Fl_Button(170, 140, 130, 50, "stop");
  stop_btn->labelsize(36);
  stop_btn->callback(stop, 0);

  Fl_Output *screen_num = new Fl_Output(70, 220, 60, 30, "Page");
  screen_num->labelsize(14);

  Fl_Input *next_screen = new Fl_Input(70, 270, 60, 30, "Next");
  next_screen->labelsize(14);
  next_screen->callback(change_screen, (void*)screen_num);

  window->end();

#endif
	ModbusClientThread modbus_interface;
	mb = &modbus_interface;
	boost::thread monitor_modbus(boost::ref(modbus_interface));

	usleep(1000);

	Fl_Window *window  = gui.make_window();
	window->show(argc, argv);
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
				}
			}
			delete um;
		//  char buf[10];
		//  sprintf(buf, "%d", modbus_interface.tab_rw_rq_registers[1074]);
		//  screen_num->value(buf);
		}
		usleep(5000);
	}

	return Fl::run();
}

