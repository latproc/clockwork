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

class MB {

public:
    modbus_t *ctx;
    uint8_t *tab_rq_bits;
    uint8_t *tab_rp_bits;
    uint16_t *tab_rq_registers;
    uint16_t *tab_rw_rq_registers;
    uint16_t *tab_rp_registers;

MB() {
    ctx = modbus_new_tcp("127.0.0.1", 1502);
    modbus_set_debug(ctx, TRUE);

    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n",
                modbus_strerror(errno));
        modbus_free(ctx);
		ctx = 0;
    }

    /* Allocate and initialize the different memory spaces */
	int nb = 2000;

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

~MB() {
    free(tab_rq_bits);
    free(tab_rp_bits);
    free(tab_rq_registers);
    free(tab_rp_registers);
    free(tab_rw_rq_registers);

    modbus_close(ctx);
    modbus_free(ctx);
}

};

MB *mb = 0;

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

int main(int argc, char **argv) {
  mb = new MB;

  Fl_Window *window = new Fl_Window(340,360);
  Fl_Box *box = new Fl_Box(20,40,300,100,"Hello, World!");
  box->box(FL_UP_BOX);
  box->labelfont(FL_BOLD+FL_ITALIC);
  box->labelsize(36);
  box->labeltype(FL_SHADOW_LABEL);

  Fl_Button *start_btn= new Fl_Button(20, 140, 130, 50, "start");
  start_btn->labelsize(36);
  start_btn->callback(start, 0);
  Fl_Button *stop_btn= new Fl_Button(170, 140, 130, 50, "stop");
  stop_btn->labelsize(36);
  stop_btn->callback(stop, 0);

  window->end();
  window->show(argc, argv);
  return Fl::run();
}

