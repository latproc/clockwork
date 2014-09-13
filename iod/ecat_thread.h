#ifndef __ecat_thread_h
#define __ecat_thread_h

#include "zmq.hpp"

class EtherCATThread {
public:
	static const char *ZMQ_Addr;
	enum State { e_collect, e_update };
	EtherCATThread();
    void operator()();
    void stop() { program_done = true; }
	void setCycleDelay(long delay);
private:
	State status;
	bool program_done;
	long cycle_delay;
	zmq::socket_t *cmd_sock;
	zmq::socket_t *sync_sock;

	bool waitForSync(zmq::socket_t &sync);
};

#endif
