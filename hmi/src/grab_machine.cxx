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
#include <boost/thread.hpp>
#include "grab_console.h"
#include "commands.h"
#include <zmq.hpp>
#include <value.h>
#include <MessageEncoding.h>

zmq::context_t *context;
zmq::socket_t *psocket = 0;

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
		
			
		//SEND SM_GrabConsoleE24On.TurnOn
		std::list<Value> command;
		command.push_back("SEND");
		command.push_back("SM_GrabConsoleE24On.TurnOn");
		process_command(command);
		
		command.clear();
		command.push_back("SEND");
		command.push_back("SM_GrabMotorStart.TurnOn");
		process_command(command);

		while (!finished) {
			
		}
	}

	bool finished;

};

class ModbusClientThread{

public:
    modbus_t *ctx;
    uint8_t *tab_rq_bits;
    uint8_t *tab_rp_bits;
    uint16_t *tab_rq_registers;
    uint16_t *tab_rw_rq_registers;
    uint16_t *tab_rp_registers;

	bool finished;

	ModbusClientThread() : ctx(0), tab_rq_bits(0), tab_rp_bits(0), 
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
    free(tab_rq_registers);
    free(tab_rp_registers);
    free(tab_rw_rq_registers);

    modbus_close(ctx);
    modbus_free(ctx);
}

void operator()() {
	while (!finished) {
		int rc = modbus_read_registers(ctx, 1075, 1, tab_rp_registers+1075);
		if (rc == -1) {
			fprintf(stderr, "%s\n", modbus_strerror(errno));
		}
		if (tab_rp_registers[1075] != tab_rw_rq_registers[1075]) {
			printf("requested page: %d, current %d\n", tab_rp_registers[1075], tab_rw_rq_registers[1075]);
			tab_rw_rq_registers[1075] = tab_rp_registers[1075];
			tab_rw_rq_registers[1074] = tab_rp_registers[1075];
			Fl::lock();
			const char *msg = "update";
			Fl::awake((void*)msg);
			Fl::unlock();
			rc = modbus_write_register(ctx, 1074, tab_rw_rq_registers[1074]);
			if (rc == -1) {
				fprintf(stderr, "%s\n", modbus_strerror(errno));
			}
		}
		usleep(100000);
	}
}

};

ModbusClientThread *mb = 0;


void press(Fl_Widget *w, void *data) {
	int *addr= (int*)data;
	Fl_Button *btn = dynamic_cast<Fl_Button*>(w);
	if (btn) {
		int val = btn->value();
		mb->tab_rq_bits[*addr] = val;
	}
	else
		mb->tab_rq_bits[*addr] = 1;
    int rc = modbus_write_bit(mb->ctx, *addr, mb->tab_rq_bits[*addr]);
    if (rc != 1) {
        printf("ERROR modbus_write_bit (%d)\n", rc);
        printf("Address = %d, value = %d\n", *addr, mb->tab_rq_bits[0]);
    } 
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

	
	GrabUserInterface gui;
	
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
	Fl_Window *window  = gui.make_window();
	window->show(argc, argv);
	Fl::lock();

	ModbusClientThread modbus_interface;
	mb = &modbus_interface;
	boost::thread monitor_modbus(boost::ref(modbus_interface));
	
	ClockworkClientThread cw_client;
	boost::thread monitor_cw(boost::ref(cw_client));

	while (Fl::wait() > 0) {
		if (Fl::thread_message()) {
		//  char buf[10];
		//  sprintf(buf, "%d", modbus_interface.tab_rw_rq_registers[1074]);
		//  screen_num->value(buf);
		}
	}

	return Fl::run();
}

