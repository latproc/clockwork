/*
  Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

  This file is part of Latproc

  Latproc is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
  
  Latproc is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Latproc; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <iostream>
#include <iterator>
#include <stdio.h>
#include <boost/program_options.hpp>
#include <zmq.hpp>
#include <sstream>
#include <string.h>
#include "Logger.h"
#include "DebugExtra.h"
#include <inttypes.h>
#include <fstream>
#include "symboltable.h"
#include <list>
#include <utility>
#include <boost/foreach.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include "IODCommand.h"
#include <modbus/modbus.h>
#include <bitset>
#include "MessagingInterface.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <signal.h>


namespace po = boost::program_options;

static boost::mutex q_mutex;
static boost::condition_variable_any cond;

const int MemSize = 65536;
bool debug = false;

std::stringstream dummy;
time_t last = 0;
time_t now;
#define OUT (time(&now) == last) ? dummy : std::cout

class IODInterfaceThread;
class IODCommandInterface;

IODInterfaceThread *g_iod_interface;
IODCommandInterface *g_iodcmd;
static modbus_mapping_t *modbus_mapping = 0;
static modbus_t *modbus_context = 0;

char *sendIOD(int group, int addr, int new_value);

void insert(int group, int addr, int value, int len) {
	if (group == 1)
		modbus_mapping->tab_input_bits[addr] = value;
	else if (group == 0)
		modbus_mapping->tab_bits[addr] = value;
	else if (group == 3) {
		if (len == 1) {
			modbus_mapping->tab_input_registers[addr] = value & 0xffff;
		} else if (len == 2) {
			int *l = (int32_t*) &modbus_mapping->tab_input_registers[addr];
			*l = value & 0xffffffff;
		}
	} else if (group == 4) {
		if (len == 1) {
			modbus_mapping->tab_registers[addr] = value & 0xffff;
		} else if (len == 2) {
			int *l = (int32_t*) &modbus_mapping->tab_registers[addr];
			*l = value & 0xffffffff;
		}
	}
}

// ------------ Command interface -----------

struct IODCommandUnknown : public IODCommand {
    bool run(std::vector<std::string> &params) {
        std::stringstream ss;
        ss << "Unknown command: ";
        std::ostream_iterator<std::string> oi(ss, " ");
        ss << std::flush;
        error_str = strdup(ss.str().c_str());
        return false;
    }
};

struct IODInterfaceThread {
    void operator()() {
		std::cout << "------------------ IOD Interface Thread Started -----------------\n";
        context = new zmq::context_t(1);
        if (!socket)
			socket = new zmq::socket_t(*context, ZMQ_REQ);
        socket->connect ("tcp://localhost:5555");
		is_ready = true;
    }
    IODInterfaceThread() : done(false), is_ready(false), context(0), socket(0){
	}
	~IODInterfaceThread() { 
		if (socket) delete socket;
		socket = 0;
		if (context) delete context;
		context = 0;
	}

    void stop() { done = true; }
	bool ready() { return is_ready; }
    bool done;
	bool is_ready;
    zmq::context_t *context;
    zmq::socket_t *socket;
};

struct IODCommandInterface {
	IODCommandInterface() : REQUEST_TIMEOUT(2000), REQUEST_RETRIES(3), context(0), socket(0){
	    context = new zmq::context_t(1);
		connect();
	}
	~IODCommandInterface() { 
		if (socket) delete socket;
		socket = 0;
		if (context) delete context;
		context = 0;
	}

    const int REQUEST_TIMEOUT;
    const int REQUEST_RETRIES;
    
    char *sendMessage(const char *message) {
        try {
            const char *msg = (message) ? message : "";

            int retries = REQUEST_RETRIES;
            while (retries) {
                size_t len = strlen(msg);
                zmq::message_t request (len);
                memcpy ((void *) request.data (), msg, len);
                socket->send (request);
                bool expect_reply = true;
                
                while (expect_reply) {
                    zmq::pollitem_t items[] = { { *socket, 0, ZMQ_POLLIN, 0 } };
                    zmq::poll( &items[0], 1, REQUEST_TIMEOUT*1000);
                    if (items[0].revents & ZMQ_POLLIN) {
                        zmq::message_t reply;
                        socket->recv(&reply);
	                    len = reply.size();
	                    char *data = (char *)malloc(len+1);
	                    memcpy(data, reply.data(), len);
	                    data[len] = 0;
	                    std::cout << data << "\n";
						return data;
                    }
                    else if (--retries == 0) {
                        // abandon
                        expect_reply = false;
                        std::cerr << "abandoning message " << msg << "\n";
                        delete socket;
                        connect();
                    }
                    else {
                        // retry
                        std::cerr << "retrying message " << msg << "\n";
                        delete socket;
                        connect();
                        socket->send (request);
                    }
                }
            }
        }
        catch(std::exception e) {
            std::cerr <<e.what() << "\n";
        }
		return 0;
    }

	void connect() {
		socket = new zmq::socket_t(*context, ZMQ_REQ);
	    socket->connect ("tcp://localhost:5555");
        int linger = 0; // do not wait at socket close time
        socket->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
	}

    zmq::context_t *context;
    zmq::socket_t *socket;
};

int getInt(uint8_t *p) {
	int x = *p++;
	x = (x<<8) + *p;
	return x;
}

void display(uint8_t *p, int len) {
	while (len--) printf("%02x",*p++);
	printf("\n");
}

struct ModbusServerThread {
	
    void operator()() {
		std::cout << "------------------ Modbus Server Thread Started -----------------\n";

		if ( LogState::instance()->includes(DebugExtra::instance()->DEBUG_MODBUS) )
			modbus_set_debug(modbus_context, TRUE);
			
		int function_code_offset = modbus_get_header_length(modbus_context);

		int socket = modbus_tcp_listen(modbus_context, 3);
		if (debug) std::cout << "Modbus listen socket: " << socket << "\n";
		FD_ZERO(&connections);
		FD_SET(socket, &connections);
		max_fd = socket;
		fd_set activity;
		while (!done) {
			activity = connections;
			int nfds;
			if ( (nfds = select(max_fd +1, &activity, 0, 0, 0)) == -1) {
				perror("select");
				continue; // TBD
			}
			if (nfds == 0) continue;
			for (int conn = 0; conn <= max_fd; ++conn) {
				if (!FD_ISSET(conn, &activity)) continue;
			
				if (conn == socket) { // new connection
                    struct sockaddr_in panel_in;
                    socklen_t addr_size = sizeof(panel_in);

                    memset(&panel_in, 0, sizeof(struct sockaddr_in));
					int panel_fd;
                    if ( (panel_fd = accept(socket, (struct sockaddr *)&panel_in, &addr_size)) == -1) {
                        perror("accept");
                    } else {
						int option = 1;
					    int res = setsockopt(panel_fd, IPPROTO_TCP, TCP_NODELAY, &option, sizeof(int)); 
						if (res == -1) { perror("setsockopt"); }
					    res = setsockopt(panel_fd, SOL_SOCKET, SO_KEEPALIVE, &option, sizeof(int)); 
						if (res == -1) { perror("setsockopt"); }
                        FD_SET(panel_fd, &connections);
                        if (panel_fd > max_fd) max_fd = panel_fd;;
						std::cout << "new connection: " << panel_fd << "\n";
					}

				}
				else {
		
			        uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
			        uint8_t query_backup[MODBUS_TCP_MAX_ADU_LENGTH];
			        int n; 
	
					modbus_set_socket(modbus_context, conn); // tell modbus to use this current connection
			        n = modbus_receive(modbus_context, query);
			        if (n != -1) {
						memcpy(query_backup, query, n);
						int addr = getInt( &query[function_code_offset+1]);
						//int len = getInt( &query[function_code_offset+3]);
						int fc = query[function_code_offset];
						// ensure changes to coils are not sent to iod if they are not required
						bool ignore_coil_change = false; 
						if (fc == 5) {  // coil write function
							ignore_coil_change = (query_backup[function_code_offset + 3] && modbus_mapping->tab_bits[addr]);
							if (ignore_coil_change) std::cout << "ignoring coil change " << addr 
								<< ((query_backup[function_code_offset + 3]) ? "on" : "off") << "\n";
						}

						// process the request, updating our ram as appropriate
			            n = modbus_reply(modbus_context, query, n, modbus_mapping);

						// post process - make sure iod is informed of the change
						if (fc == 2) {
							//std::cout << "connection " << conn << " read coil " << addr << "\n";
						} 
						else if (fc == 1) {
							//std::cout << "connection " << conn << " read discrete " << addr << "\n";
						}
						else if (fc == 3) {
							//std::cout << "connection " << conn << " got rw_register " << addr << "\n";
						}
						else if (fc == 4) {
							//std::cout << "connection " << conn << " got register " << addr << "\n";
						}
						else if (fc == 15) {
							//std::cout << "connection " << conn << " request multi addr " << addr << " n:" << len << "\n";
						}
						else if (fc == 16) {
							//std::cout << "write multiple register " << addr << " n=" << len << "\n";
						}
						else if (fc == 5) {
							if (!ignore_coil_change) {
								char *res = sendIOD(0, addr+1, (query_backup[function_code_offset + 3]) ? 1 : 0);
								if (res) free(res);
								if (debug) std::cout << " Updating coil " << addr << " from connection " << conn 
										<< ((query_backup[function_code_offset + 3]) ? "on" : "off") << "\n";
							}
						}
						else if (fc == 6) {
							int val = getInt( &query[function_code_offset+5]);
							char *res = sendIOD(4, addr+1, modbus_mapping->tab_registers[addr]);
							if (res) free(res);
							if (debug) std::cout << " Updating register " << addr << " to " << val << " from connection " << conn << "\n";
						}
						else 
							std::cout << " function code: " << (int)query_backup[function_code_offset] << "\n";
						if (n == -1) {
							if (debug) std::cout << "Error: " << modbus_strerror(errno) << "\n";

						}
			        } else {
			            /* Connection closed by the client or error */
						std::cout << "Error: " << modbus_strerror(errno) << "\n";
						std::cout << "Modbus connection " << conn << " lost\n";
						close(conn);
						if (conn == max_fd) --max_fd;
						FD_CLR(conn, &connections);
			        }
				}
		    }
		}
		modbus_free(modbus_context);
		
    }
    ModbusServerThread() : done(false) {
		FD_ZERO(&connections);
    }
	~ModbusServerThread() {
		if (modbus_mapping) modbus_mapping_free(modbus_mapping);
		if (modbus_context) modbus_free(modbus_context);
	}

    void stop() { done = true; }
    bool done;
	fd_set connections;
	int max_fd;
};

char *sendIOD(int group, int addr, int new_value) {
	std::stringstream ss;
	ss << "MODBUS " << group << " " << addr << " " << new_value;
	std::string s(ss.str());
	if (g_iod_interface) 
		return g_iodcmd->sendMessage(s.c_str());
	else 	
		return strdup("IOD interface not ready\n");
}

void loadData(const char *initial_settings) {
	std::istringstream init(initial_settings);

	char buf[200];
    while (init.getline(buf, 200, '\n')) {
        std::istringstream iss(buf);
        int group, addr, len;
		int value;
		std::string name;
        iss >> group >> addr >> name >> len >> value;
		if (debug) std::cout << name << ": " << group << " " << addr << " " << len << " " << value <<  "\n";
		insert(group, addr-1, value, len);
	}
}

bool done = false;

static void finish(int sig)
{
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGINT, &sa, 0);
    done = true;
}

bool setup_signals()
{
    struct sigaction sa;
    sa.sa_handler = finish;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, 0) || sigaction(SIGINT, &sa, 0)) {
        return false;
    }
    return true;
}

int main(int argc, const char * argv[]) {

	po::options_description desc("Allowed options");
	desc.add_options()
	("help", "produce help message")
	("debug","enable debug")
	;
	po::variables_map vm;   
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);    
	if (vm.count("help")) {
		std::cout << desc << "\n";
		return 1;
	}
	int port = 5558;
	if (argc > 2 && strcmp(argv[1],"-p") == 0) {
		port = strtol(argv[2], 0, 0);
	}

	if (vm.count("debug")) LogState::instance()->insert(DebugExtra::instance()->DEBUG_MODBUS);

	modbus_mapping = modbus_mapping_new(10000, 10000, 10000, 10000);
	if (!modbus_mapping) {
		std::cerr << "Error setting up modbus mapping regions\n";
		modbus_free(modbus_context);
		modbus_context = 0;
		return 1;
	}
	modbus_context = modbus_new_tcp("localhost", 1502);
	if (!modbus_context) {
		std::cerr << "Error creating a libmodbus TCP interface\n";
		return 1;
	}

	std::cout << "-------- Starting Command Interface ---------\n";	
	g_iodcmd = new IODCommandInterface;
	
	// initialise memory
	{
		std::cout << "-------- Collecting IO Status ---------\n";
		char *initial_settings;
		do {	
			initial_settings = g_iodcmd->sendMessage("MODBUS REFRESH");
			if (initial_settings) {
				loadData(initial_settings);
				free(initial_settings);
			}
			else
				sleep(2);
		} while (!initial_settings);
	}

	std::cout << "-------- Starting to listen to IOD ---------\n";	
	IODInterfaceThread iod_interface;
	g_iod_interface = &iod_interface;
	boost::thread monitor(boost::ref(iod_interface));

	std::cout << "-------- Starting Modbus Interface ---------\n";	

	ModbusServerThread modbus_interface;
	boost::thread monitor_modbus(boost::ref(modbus_interface));

	setup_signals();
	
    try {
        
		std::cout << "Listening on port " << port << "\n";
        // client
        int res;
		std::stringstream ss;
		ss << "tcp://localhost:" << port;
        zmq::context_t context (1);
        zmq::socket_t subscriber (context, ZMQ_SUB);
        subscriber.connect(ss.str().c_str());
        //subscriber.connect("ipc://ecat.ipc");
        res = zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "", 0);
        assert (res == 0);
        while (!done) {
            zmq::message_t update;
            subscriber.recv(&update);
			//std::cout << "received: " << static_cast<char*>(update.data());
            std::istringstream iss(static_cast<char*>(update.data()));
			std::string cmd;
			iss >> cmd;
			if (cmd == "UPDATE") {
            	int group, addr, len, value;
				std::string name;
            	iss >> group >> addr >> name >> len >> value;
	
				if (debug) std::cout << "IOD: " << group << " " << addr << " " << name << " " << len << " " << value <<  "\n";
				insert(group, addr-1, value, len);
			}
			else if (cmd == "STARTUP") {
#if 1
				sleep(2);
				break;		
#else
				char *initial_settings = g_iodcmd->sendMessage("MODBUS REFRESH");
				loadData(initial_settings);
				free(initial_settings);
#endif
			}
			else if (cmd == "DEBUG") {
				std::string which;
				if(iss >> which) {
					debug = (which == "ON") ? true : false;
				}
			}
        }
    }
    catch(std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    catch(...) {
        std::cerr << "Exception of unknown type!\n";
    }

    iod_interface.stop();
    monitor.join();
	modbus_interface.stop(); // may hang if clients are connected
	monitor_modbus.join();
    return 0;
}

