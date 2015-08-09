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

/*
   modbusd starts a thread that accepts connections from modbus master
   programs and starts a subscriber on clockwork to listen for
   changes to items that are exported to modbus.
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
#include "value.h"
#include <list>
#include <utility>
#include <boost/foreach.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include "IODCommand.h"
#include <modbus/modbus.h>
#include <bitset>
#include "cJSON.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <signal.h>
#include "MessageEncoding.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "ConnectionManager.h"

namespace po = boost::program_options;

static boost::mutex q_mutex;
static boost::condition_variable_any cond;

const char *local_commands = "inproc://local_cmds";

enum ProgramState { s_initialising, s_running, s_finished } program_state = s_initialising;

int debug = 0;
int saved_debug = 0;

const int MemSize = 65536;
const int COILS 	= 1;
const int REGS 		= 1 << 1;
const int TOPLC 	= 1 << 2;
const int TOPANEL = 1 << 3;
const int IOD			= 1 << 4;

const int STRTYPE	= 1 << 5;

const int VERBOSE_TOPLC 	= 1 << 8;

const int DEBUG_LIB 		= 1 << 15;

const int DEBUG_ALL = 0xffff;
const int NOTLIB = DEBUG_ALL ^ DEBUG_LIB;

#define DEBUG_STRINGS (debug & STRTYPE)
#define DEBUG_VERBOSE_TOPLC (debug & VERBOSE_TOPLC)
#define DEBUG_BASIC ( debug & NOTLIB )
#define DEBUG_ANY ( debug )

std::stringstream dummy;
time_t last = 0;
time_t now;
#define OUT (time(&now) == last) ? dummy : std::cout

static modbus_mapping_t *modbus_mapping = 0;
static modbus_t *modbus_context = 0;

static std::set<std::string> active_addresses;
static std::map<std::string, bool> initialised_address;


void activate_address(std::string& addr_str)
{
	if (active_addresses.find(addr_str) == active_addresses.end())
	{
		active_addresses.insert(addr_str);
		initialised_address[addr_str] = false;
	}
	else
		if (DEBUG_BASIC)
			std::cerr << "(fyi) address " << addr_str << " already active\n";
}

void insert(int group, int addr, const char *value, size_t len)
{
	if (DEBUG_BASIC) std::cout << "g:"<<group<<addr<<" "<<value<<" " << len << "\n";
	char buf[20];
	snprintf(buf, 19, "%d.%d", group, addr);
	std::string addr_str(buf);
	//if (len % 2 != 0) len++;// pad
	uint16_t *dest = 0;
	//	int str_len = strlen(value);

	if (group == 3)
		dest = &modbus_mapping->tab_input_registers[addr];
	else if (group == 4)
		dest = &modbus_mapping->tab_registers[addr];
	if (!dest) {
		std::cerr << "no entry for " << addr << " in group 3 or 4\n";
		return;
	}
	uint8_t *p = (uint8_t *)value;
	if (DEBUG_STRINGS) std::cout << "string length: " << len << "\n";
	uint8_t *q = (uint8_t*)dest;
	unsigned int i=0;
	for (i=0; i<len/2; ++i)
	{
		*q++ = *(p+1);
		*q++ = *p++;
		p++;

		//		if (i+1<str_len) *q++ = *(p+1); else *q++ = 0;
		//		if (i<str_len) *q++ = *p++; else *q++ = 0;
		//		if (i+1<str_len) p++;
	}
	// if the length was odd we will not have copied the terminating null
	*q++ = 0;
	if (len % 2)
		*q = 0;
}
void insert(int group, int addr, int value, size_t len)
{
	if (DEBUG_BASIC) std::cout << "g:"<<group<<addr<<" "<<value<<" " << len << "\n";
	char buf[20];
	snprintf(buf, 19, "%d.%d", group, addr);
	std::string addr_str(buf);
	if (group == 1)
	{
		modbus_mapping->tab_input_bits[addr] = value;

		if (DEBUG_BASIC)
			std::cout << "Updated Modbus memory for input bit " << addr << " to " << value << "\n";
		activate_address(addr_str);
	}
	else if (group == 0)
	{
		modbus_mapping->tab_bits[addr] = value;
		if (DEBUG_BASIC) std::cout << "Updated Modbus memory for bit " << addr << " to " << value << "\n";
		activate_address(addr_str);
	}
	else if (group == 3)
	{
		if (len == 1)
		{
			modbus_mapping->tab_input_registers[addr] = value & 0xffff;
			activate_address(addr_str);
		}
		else if (len == 2)
		{
			int *l = (int32_t*) &modbus_mapping->tab_input_registers[addr];
			*l = value & 0xffffffff;
			activate_address(addr_str);
		}
	}
	else if (group == 4)
	{
		if (len == 1)
		{
			modbus_mapping->tab_registers[addr] = value & 0xffff;
			if (DEBUG_BASIC) std::cout << "Updated Modbus memory for reg " << addr << " to " << (value  & 0xffff) << "\n";
			activate_address(addr_str);
		}
		else if (len == 2)
		{
			if (DEBUG_BASIC) std::cout << "Updated Modbus memory for 32 bit reg " << addr << " to " << (value  & 0xffffffff) << "\n";
			int *l = (int32_t*) &modbus_mapping->tab_registers[addr];
			*l = value & 0xffffffff;
			activate_address(addr_str);
		}
	}
}

/*
// ------------ Command interface -----------

struct IODCommandUnknown : public IODCommand {
bool run(std::vector<Value> &params) {
std::stringstream ss;
ss << "Unknown command: ";
std::ostream_iterator<std::string> oi(ss, " ");
ss << std::flush;
error_str = strdup(ss.str().c_str());
return false;
}
};
*/

int getInt(uint8_t *p)
{
	int x = *p++;
	x = (x<<8) + *p;
	return x;
}

uint32_t getInt32(uint8_t *p)
{
	uint32_t x = *p++;
	x = (x<<8) + *p++;
	x = (x<<8) + *p++;
	x = (x<<8) + *p;
	return x;
}

void display(uint8_t *p, int len)
{
	while (len--) printf("%02x",*p++);
	printf("\n");
}

static unsigned long start_t = 0;

std::ostream &timestamp(std::ostream &out)
{
	struct timeval now;
	gettimeofday(&now, 0);
	unsigned long t = now.tv_sec*1000000 + now.tv_usec - start_t;
	return out << (t / 1000) ;
}

std::string getIODSyncCommand(int group, int addr, bool new_value);
std::string getIODSyncCommand(int group, int addr, int new_value);
std::string getIODSyncCommand(int group, int addr, unsigned int new_value);


struct ModbusServerThread
{

	void operator()()
	{
		std::cout << "------------------ Modbus Server Thread Started -----------------\n" << std::flush;
		cmd_interface = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
		cmd_interface->connect(local_commands);

		int function_code_offset = 0; //modbus_get_header_length(modbus_context);

		int paused_counter = 0;
		int warn_at = 10;
		while (modbus_state != ms_finished)
		{
			if (modbus_state == ms_pausing) {
				std::cout << "pausing modbus: cleaning old..\n" << std::flush;
				//if (modbus_mapping) modbus_mapping_free(modbus_mapping);
				if (modbus_context) modbus_free(modbus_context);
				modbus_context = 0;

				modbus_state = ms_paused;
				paused_counter = 0;
			}
			if (modbus_state == ms_paused) { 
				if (++paused_counter>warn_at) {
					std::cout << "modbus paused\n" << std::flush;
					warn_at += 10;
					usleep(100000); 
				}
				continue; 
			}
			else { paused_counter = 0; warn_at = 10; }
			if (modbus_state == ms_starting || modbus_state == ms_resuming) {
				if (!modbus_context) modbus_context = modbus_new_tcp("0.0.0.0", 1502);

				function_code_offset = modbus_get_header_length(modbus_context);

				if ( LogState::instance()->includes(DebugExtra::instance()->DEBUG_MODBUS) )
					modbus_set_debug(modbus_context, TRUE);
				else
					modbus_set_debug(modbus_context, FALSE);

				std::cout << "starting modbus_tcp_listen\n" << std::flush;
				socket = modbus_tcp_listen(modbus_context, 3);
				std::cout << "finished modbus_tcp_listen " << socket << "\n" << std::flush;
				if (socket == -1) {
					perror("modbus listen");
					continue;
				}
				if (debug) std::cout << "Modbus listen socket: " << socket << "\n" << std::flush;
				FD_ZERO(&connections);
				FD_SET(socket, &connections);
				max_fd = socket;
				modbus_state = ms_collecting;
				continue;
			}
			fd_set activity;
			activity = connections;
			int nfds;
			struct timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 100000;
			if ( (nfds = select(max_fd +1, &activity, 0, 0, &timeout)) == -1)
			{
				perror("select");
				if (errno == EINVAL || errno == ENOENT) { modbus_state = ms_finished; break; }
				usleep(100);
				continue; // TBD
			}
			if (nfds == 0) { /*if (debug) std::cout << "idle\n"; */ usleep(1000); continue; }
			for (int conn = 0; conn <= max_fd; ++conn)
			{
				if (!FD_ISSET(conn, &activity)) continue;
				if (conn == socket)   // new connection
				{
					std::cout << "new modbus connection on socket " << socket << "\n" << std::flush;
					struct sockaddr_in panel_in;
					socklen_t addr_size = sizeof(panel_in);

					memset(&panel_in, 0, sizeof(struct sockaddr_in));
					int panel_fd;
					if ( (panel_fd = accept(socket, (struct sockaddr *)&panel_in, &addr_size)) == -1)
					{
						perror("accept");
					}
					else
					{
						connection_list.push_back(panel_fd);
						int option = 1;
						int res = setsockopt(panel_fd, IPPROTO_TCP, TCP_NODELAY, &option, sizeof(int));
						if (res == -1)
						{
							perror("setsockopt");
						}
						res = setsockopt(panel_fd, SOL_SOCKET, SO_KEEPALIVE, &option, sizeof(int));
						if (res == -1)
						{
							perror("setsockopt");
						}
						FD_SET(panel_fd, &connections);
						if (panel_fd > max_fd) max_fd = panel_fd;;
						std::cout << timestamp << " new connection: " << panel_fd << "\n" << std::flush;
					}
				}
				else
				{
					uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
					uint8_t query_backup[MODBUS_TCP_MAX_ADU_LENGTH];
					int n;

					modbus_set_socket(modbus_context, conn); // tell modbus to use this current connection
					n = modbus_receive(modbus_context, query);
					char *previous_value = 0; // used to see if a string value is different between ram and connection
					if (n != -1)
					{
						std::list<std::string>iod_sync_commands;
						memcpy(query_backup, query, n);
						int addr = getInt( &query[function_code_offset+1]);
						//int len = getInt( &query[function_code_offset+3]);
						int fc = query[function_code_offset];
						// ensure changes to coils are not sent to iod if they are not required
						bool ignore_coil_change = false;
						if (fc == 3) // register read - why are we keeping the previous value?
						{
#if 0
							int num_regs = query[function_code_offset+4];
							char *src = (char*) &modbus_mapping->tab_registers[addr]; //(char*) &query[function_code_offset+6];
							previous_value = (char *)malloc(2*num_regs+1);
							char *dest = previous_value;
							for (int i=0; i<num_regs; ++i)
							{
								*dest++ = *(src+1);
								*dest++ = *src++;
								src++;
							}
#endif
						}
						else if (fc == 5) // coil write function
						{
							ignore_coil_change = (query_backup[function_code_offset + 3] && modbus_mapping->tab_bits[addr]);
							if (DEBUG_BASIC && ignore_coil_change) std::cout << "ignoring coil change " << addr
								<< ((query_backup[function_code_offset + 3]) ? "on" : "off") << "\n";
						}
						else if (fc == 15)
						{
							int num_coils = getInt( &query[function_code_offset+3]);
							int num_bytes = query_backup[function_code_offset+5];
							int curr_coil = 0;
							unsigned char *data = query_backup + function_code_offset + 6;
							for (int b = 0; b<num_bytes; ++b)
							{
								for (int bit = 0; bit < 8; ++bit)
								{
									if (curr_coil >= num_coils) break;
									char buf[20];
									snprintf(buf, 19, "%d.%d", 0, addr);
									std::string addr_str(buf);

									if (active_addresses.find(addr_str) != active_addresses.end())
									{
										if (DEBUG_BASIC) std::cout << "updating cw with new discrete: " << addr
											<< " (" << (int)(modbus_mapping->tab_bits[addr]) <<")"
												<< " (" << (int)(modbus_mapping->tab_input_bits[addr]) <<")"
												<< "\n";
										unsigned char val = (*data) & (1<<bit);


										if ( !initialised_address[addr_str] ||  ( val != 0 && modbus_mapping->tab_bits[addr] == 0 )
												|| (!val && modbus_mapping->tab_bits[addr] ) )
										{
											if (DEBUG_BASIC) std::cout << "setting iod address " << addr+1 << " to " << ( (val) ? 1 : 0) << "\n";
											iod_sync_commands.push_back( getIODSyncCommand(0, addr+1, (val) ? true : false) );
											initialised_address[addr_str] = true;
										}
									}
									++addr;
									++curr_coil;
								}
								++data;
							}
						}
						else if (fc == 16)
						{
							int num_words = getInt(&query_backup[function_code_offset+3]);
							//int num_bytes = query_backup[function_code_offset+5];
							unsigned char *data = query_backup + function_code_offset + 6; // interpreted as binary
							for (int reg = 0; reg<num_words; ++reg)
							{
								char buf[20];
								snprintf(buf, 19, "%d.%d", 4, addr);
								std::string addr_str(buf);

								if (active_addresses.find(addr_str) != active_addresses.end())
								{
									int val = 0;
									//if (num_words == 1) {
										val = getInt(data);
										data += 2;
									/*}
									else if (num_words == 2) {
										val = getInt32(data);
										data += 4;
									}
									*/
									if (!initialised_address[addr_str] || val != modbus_mapping->tab_registers[addr])
									{
										if (debug & DEBUG_LIB)
											std::cout << " Updating register " << addr
											<< " to " << val << " from connection " << conn << "\n";
										iod_sync_commands.push_back( getIODSyncCommand(4, addr+1, val) );
										initialised_address[addr_str] = true;
									}
								}
								++addr;
							}
						}

						// process the request, updating our ram as appropriate
						n = modbus_reply(modbus_context, query, n, modbus_mapping);

						// post process - make sure iod is informed of the change
						if (fc == 2)
						{
							//if (DEBUG_VERBOSE_TOPLC)
							//   std::cout << timestamp << " connection " << conn << " read coil " << addr << "\n";
						}
						else if (fc == 1)
						{
							int num_coils = getInt( &query[function_code_offset+3]);
							char buf[20];
							snprintf(buf, 19, "%d.%d", 1, addr);
							std::string addr_str(buf);
							if (active_addresses.find(addr_str) != active_addresses.end() )
								if (debug & DEBUG_LIB) {
									std::cout << timestamp << " connection " << conn << " read " << num_coils << " discrete " << addr
										<< " (";
									for (int bitn=0; bitn<num_coils; ++bitn)
										std::cout  << (int)(modbus_mapping->tab_input_bits[addr+bitn])<<" ";
									std::cout <<")\n";
								}
						}
						else if (fc == 3)
						{
#if 0
							if (DEBUG_VERBOSE_TOPLC)
								std::cout << timestamp << " connection " << conn << " got rw_register " << addr << "\n";
							int num_regs = query_backup[function_code_offset+4];
							char *src = (char*) &modbus_mapping->tab_registers[addr];
							char *new_value = (char *)malloc(2*num_regs);
							char *dest = new_value;
							for (int i=0; i<num_regs; ++i)
							{
								*dest++ = *(src+1);
								*dest++ = *src++;
								src++;
							}
							bool changed = false;
							for (int i=0; i<num_regs; ++i) {
								if ( ((uint16_t*)(dest))[i] != modbus_mapping->tab_registers[addr+i])
									if (DEBUG_BASIC) std::cout << "connection " << conn << " num regs: " << num_regs << " changed rw register " << (addr+i) << "\n";
							}
							free(new_value);
							free(previous_value);
#endif
						}
						else if (fc == 4)
						{
							int num_regs = query_backup[function_code_offset+5];
							if (DEBUG_VERBOSE_TOPLC)
								if (DEBUG_BASIC) std::cout << timestamp << " connection " << conn
									<< " code: " << fc << " num regs: " << num_regs << " got register " << addr << "\n";
						}
						else if (fc == 15 || fc == 16)
						{
							if (fc == 15)
							{
								char buf[20];
								snprintf(buf, 19, "%d.%d", 0, addr);
								std::string addr_str(buf);
								if (active_addresses.find(addr_str) != active_addresses.end() )
									if (DEBUG_BASIC)
										std::cout << timestamp << " connection " << conn << " write multi discrete "
											<< addr << "\n"; //<< " n:" << len << "\n";
							}
							else if (fc == 16)
							{
								if (DEBUG_BASIC)
									std::cout << timestamp << " write multiple register " << addr  << "\n";
							}
							std::list<std::string>::iterator iter = iod_sync_commands.begin();
							while (iter != iod_sync_commands.end())
							{
								std::string cmd = *iter++;
								std::string response;
								uint64_t cmd_timeout = 250;
								if (!sendMessage(cmd.c_str(), *cmd_interface, response, 150) ) {
									std::cerr << "Message send of " << cmd << " failed\n";
								}
								//char *res = sendIODMessage(cmd);
								//if (res) free(res);
							}
							iod_sync_commands.clear();
						}
						else if (fc == 5)
						{
							if (!ignore_coil_change)
							{
								std::string cmd( getIODSyncCommand(0, addr+1, (query_backup[function_code_offset + 3]) ? 1 : 0) );
								std::string response;
								uint64_t cmd_timeout = 250;
								if (!sendMessage(cmd.c_str(), *cmd_interface, response, cmd_timeout)) {
									std::cerr << "Message send of " << cmd << " failed\n";
								}
								//char *res = sendIOD(0, addr+1, (query_backup[function_code_offset + 3]) ? 1 : 0);
								//if (res) free(res);
								if (DEBUG_BASIC)
									std::cout << timestamp << " Updating coil " << addr << " from connection " << conn
										<< ((query_backup[function_code_offset + 3]) ? " on" : " off") << "\n";
							}
						}
						else if (fc == 6)
						{
							int val = getInt( &query[function_code_offset+5]);
							std::string res(getIODSyncCommand(4, addr+1, modbus_mapping->tab_registers[addr]) );
							//                sendIOD(4, addr+1, modbus_mapping->tab_registers[addr]) );
							//if (res) free(res);
							if (DEBUG_BASIC)
								std::cout << timestamp << " Updating register " << addr << " to " << val << " from connection " << conn << "\n";
						}
						else
							if (DEBUG_BASIC)
								std::cout << timestamp << " function code: " << (int)query_backup[function_code_offset] << "\n";
						if (n == -1)
						{
							if (DEBUG_BASIC)
								std::cout << timestamp << " Error: " << modbus_strerror(errno) << "\n";
						}
					}
					else
					{
						/* Connection closed by the client or error */
						std::cout << timestamp << " Error: " << modbus_strerror(errno) << "\n";
						std::cout << timestamp << " Modbus connection " << conn << " lost\n";
						close(conn);
						connection_list.remove(conn);
						if (conn == max_fd) --max_fd;
						FD_CLR(conn, &connections);
					}
				}
			}
		}
		modbus_free(modbus_context);
		modbus_context = 0;

	}
	ModbusServerThread() : modbus_state(ms_paused), socket(0), cmd_interface(0)
	{
		FD_ZERO(&connections);

	}
	~ModbusServerThread()
	{
		if (modbus_mapping) modbus_mapping_free(modbus_mapping);
		if (modbus_context) modbus_free(modbus_context);
	}

	void stop() { modbus_state = ms_finished; }
	void pause() {
		std::cout << "Pausing modbus interface\n";
		modbus_state = ms_paused;
		shutdown(socket, SHUT_RDWR);
		close(socket);
		std::cout << "closed listening socket\n";
		modbus_free(modbus_context);
		modbus_context = 0;

		std::list<int>::iterator iter = connection_list.begin();
		while (iter != connection_list.end()) {
			shutdown(*iter, SHUT_RDWR);
			close(*iter);
			std::cout << "closed connection socket " << *iter << "\n";
			iter = connection_list.erase(iter);
		}
	}
	void resume() {
		modbus_state = ms_resuming;
	}

	enum ModbusState { ms_starting, ms_collecting, ms_running, ms_pausing, ms_paused, ms_resuming, ms_finished } modbus_state;
	fd_set connections;
	int max_fd;
	int socket;
	zmq::socket_t *cmd_interface;
	std::list<int> connection_list;

};

MessagingInterface *g_iodcmd;

char *sendIOD(int group, int addr, int new_value);
char *sendIODMessage(const std::string &s);

std::string getIODSyncCommand(int group, int addr, bool which)
{
	int new_value = (which) ? 1 : 0;
	char *msg = MessageEncoding::encodeCommand("MODBUS", group, addr, new_value);
	sendIODMessage(msg);

	if (DEBUG_BASIC) std::cout << "IOD command: " << msg << "\n";
	std::string s(msg);
	free(msg);
	return s;
}

std::string getIODSyncCommand(int group, int addr, int new_value)
{
	char *msg = MessageEncoding::encodeCommand("MODBUS", group, addr, new_value);
	sendIODMessage(msg);

	if (DEBUG_BASIC) std::cout << "IOD command: " << msg << "\n";
	std::string s(msg);
	free(msg);
	return s;
}

std::string getIODSyncCommand(int group, int addr, unsigned int new_value)
{
	char *msg = MessageEncoding::encodeCommand("MODBUS", group, addr, new_value);
	sendIODMessage(msg);

	if (DEBUG_BASIC) std::cout << "IOD command: " << msg << "\n";
	std::string s(msg);
	free(msg);
	return s;
}

char *sendIOD(int group, int addr, int new_value)
{
	std::string s(getIODSyncCommand(group, addr, new_value));
	if (g_iodcmd)
		return g_iodcmd->send(s.c_str());
	else
	{
		if (DEBUG_BASIC) std::cout << "IOD interface not ready\n";
		return strdup("IOD interface not ready\n");
	}
}

char *sendIODMessage(const std::string &s)
{
	if (g_iodcmd)
		return g_iodcmd->send(s.c_str());
	else
	{
		if (DEBUG_BASIC) std::cout << "IOD interface not ready\n";
		return strdup("IOD interface not ready\n");
	}
}

void loadData(const char *initial_settings)
{
	cJSON *obj = cJSON_Parse(initial_settings);
	if (!obj)
	{
		std::istringstream init(initial_settings);

		char buf[200];
		while (init.getline(buf, 200, '\n'))
		{
			std::istringstream iss(buf);
			int group, addr, len;
			int value;
			std::string name;
			iss >> group >> addr >> name >> len >> value;
			if (debug) std::cout << name << ": " << group << " " << addr << " " << len << " " << value <<  "\n";
			insert(group, addr-1, value, len);
		}
	}
	else
	{
		int num_params = cJSON_GetArraySize(obj);
		if (num_params)
		{
			for (int i=0; i<num_params; ++i)
			{
				cJSON *item = cJSON_GetArrayItem(obj, i);
				if (item->type == cJSON_Array)
				{
					Value group = MessageEncoding::valueFromJSONObject(cJSON_GetArrayItem(item, 0), 0);
					Value addr = MessageEncoding::valueFromJSONObject(cJSON_GetArrayItem(item, 1), 0);
					Value name = MessageEncoding::valueFromJSONObject(cJSON_GetArrayItem(item, 2), 0);
					Value len = MessageEncoding::valueFromJSONObject(cJSON_GetArrayItem(item, 3), 0);
					Value value = MessageEncoding::valueFromJSONObject(cJSON_GetArrayItem(item, 4), 0);
					if (DEBUG_BASIC)
						std::cout << name << ": " << group << " " << addr << " " << len << " " << value <<  "\n";
					if (value.kind == Value::t_string) {
						std::string valstr = value.asString();
						insert((int)group.iValue, (int)addr.iValue-1, valstr.c_str(), valstr.length()+1); // note copying null
					}
					else
						insert((int)group.iValue, (int)addr.iValue-1, (int)value.iValue, len.iValue);
				}
				else
				{
					char *node = cJSON_Print(item);
					std::cerr << "item " << i << " is not of the expected format: " << node << "\n";
					free(node);
				}
			}
		}
	}
}

static void finish(int sig)
{
	struct sigaction sa;
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGUSR1, &sa, 0);
	sigaction(SIGUSR2, &sa, 0);
	program_state = s_finished;
}

static void toggle_debug(int sig)
{
	if (debug && debug != saved_debug) saved_debug = debug;
	if (debug) debug = 0; else {
		if (saved_debug==0) saved_debug = NOTLIB ^ VERBOSE_TOPLC;
		debug = saved_debug;
	}
}

static void toggle_debug_all(int sig)
{
	if (debug) debug = 0; else debug = DEBUG_ALL; 
}

bool setup_signals()
{
	struct sigaction sa;
	sa.sa_handler = finish;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGTERM, &sa, 0) || sigaction(SIGINT, &sa, 0)) { return false; }
	sa.sa_handler = toggle_debug;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGUSR1, &sa, 0) ) { return false; }
	sa.sa_handler = toggle_debug_all;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGUSR2, &sa, 0) ) { return false; }
	return true;
}

size_t parseIncomingMessage(const char *data, std::vector<Value> &params) // fillin params
{
	size_t count =0;
	std::list<Value> parts;
	std::string ds;
	std::list<Value> *param_list = 0;
	if (MessageEncoding::getCommand(data, ds, &param_list))
	{
		params.push_back(ds);
		if (param_list)
		{
			std::list<Value>::const_iterator iter = param_list->begin();
			while (iter != param_list->end())
			{
				const Value &v  = *iter++;
				params.push_back(v);
			}
		}
		count = params.size();
		delete param_list;
	}
	else
	{
		std::istringstream iss(data);
		while (iss >> ds)
		{
			parts.push_back(ds.c_str());
			++count;
		}
		std::copy(parts.begin(), parts.end(), std::back_inserter(params));
	}
	return count;
}

void CollectModbusStatus() {
	int tries = 3;
	std::cout << "-------- Collecting IO Status ---------\n" << std::flush;
	char *initial_settings = 0;
	do
	{
		active_addresses.clear();
		initialised_address.clear();
		initial_settings = g_iodcmd->sendCommand("MODBUS", "REFRESH");
		if (initial_settings && strncasecmp(initial_settings, "ignored", strlen("ignored")) != 0)
		{
			loadData(initial_settings);
			free(initial_settings);
		}
		else
			sleep(2);
	}
	while (!initial_settings && --tries > 0);
}

ModbusServerThread *modbus_interface_thread = 0;

class SetupDisconnectMonitor : public EventResponder {
	public:
		void operator()(const zmq_event_t &event_, const char* addr_) {
			if (modbus_interface_thread) 
				modbus_interface_thread->pause();
		}
};

static bool need_refresh = false;

class SetupConnectMonitor : public EventResponder {
	public:
		void operator()(const zmq_event_t &event_, const char* addr_) {
			need_refresh = true;
		}
};

int main(int argc, const char * argv[])
{
	zmq::context_t context;
	MessagingInterface::setContext(&context);

	struct timeval now;
	gettimeofday(&now, 0);
	start_t = now.tv_sec*1000000 + now.tv_usec;
	int cw_port;
	std::string hostname;

	po::options_description desc("Allowed options");
	desc.add_options()
		("help", "produce help message")
		("debug",po::value<int>(&debug)->default_value(0), "set debug level")
		("host", po::value<std::string>(&hostname)->default_value("localhost"),"remote host (localhost)")
		("cwout",po::value<int>(&cw_port)->default_value(5555), "clockwork outgoing port (5555)")
		("cwin", "clockwork incoming port (deprecated)")
		;
	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);
	if (vm.count("help"))
	{
		std::cout << desc << "\n";
		return 1;
	}
	int cw_out = 5555;
	std::string host("127.0.0.1");
	// backward compatibility
	if (argc > 2 && strcmp(argv[1],"-p") == 0)
	{
		std::cerr << "NOTICE: the -p option is deprecated\n";
	}
	if (vm.count("cwout")) cw_out = vm["cwout"].as<int>();
	if (vm.count("host")) host = vm["host"].as<std::string>();
	if (vm.count("debug")) debug = vm["debug"].as<int>();


	if (debug & DEBUG_LIB)
	{
		LogState::instance()->insert(DebugExtra::instance()->DEBUG_MODBUS);
	}

	modbus_mapping = modbus_mapping_new(10000, 10000, 10000, 10000);
	if (!modbus_mapping)
	{
		std::cerr << "Error setting up modbus mapping regions\n";
		modbus_free(modbus_context);
		modbus_context = 0;
		return 1;
	}
	std::cout << "-------- Starting Command Interface ---------\n" << std::flush;
	std::cout << "connecting to clockwork on " << host << ":" << cw_out << "\n";
	g_iodcmd = MessagingInterface::create(host, cw_out);
	g_iodcmd->start();

	program_state = s_running;
	setup_signals();

	std::cout << "-------- Starting Modbus Interface ---------\n" << std::flush;

	bool modbus_started = false;
	ModbusServerThread modbus_interface;
	modbus_interface_thread = &modbus_interface;
	boost::thread monitor_modbus(boost::ref(modbus_interface));

	// the local command channel accepts commands from the modbus thread and relays them to iod.
	zmq::socket_t iosh_cmd(*MessagingInterface::getContext(), ZMQ_REP);
	iosh_cmd.bind(local_commands);

	SubscriptionManager subscription_manager("MODBUS_CHANNEL", eCLOCKWORK, "localhost", 5555);
	subscription_manager.configureSetupConnection(host.c_str(), cw_out);
	SetupDisconnectMonitor disconnect_responder;
	SetupConnectMonitor connect_responder;
	subscription_manager.monit_setup->addResponder(ZMQ_EVENT_DISCONNECTED, &disconnect_responder);
	subscription_manager.monit_setup->addResponder(ZMQ_EVENT_CONNECTED, &connect_responder);
	subscription_manager.setupConnections();

	try
	{
		int exception_count = 0;
		int error_count = 0;
		while (program_state != s_finished)
		{

			zmq::pollitem_t items[] =
			{
				{ subscription_manager.setup(), 0, ZMQ_POLLIN, 0 },
				{ subscription_manager.subscriber(), 0, ZMQ_POLLIN, 0 },
				{ iosh_cmd, 0, ZMQ_POLLIN, 0 }
			};
			try {
				if (!subscription_manager.checkConnections(items, 3, iosh_cmd)) {
					if (debug) std::cout << "no connection to iod\n";
					usleep(1000000);
					exception_count = 0;
					continue;
				}

				exception_count = 0;
			}
			catch (std::exception ex) {
				std::cout << "polling connections: " << ex.what() << "\n";
				if (++exception_count <= 5 && program_state != s_finished) { usleep(400000); continue; }
				exit(0);
			}
			if (need_refresh) {
				CollectModbusStatus();
				need_refresh = false;
				std::cout << "resuming modbus\n";
#if 0
				if (!modbus_context) {
					modbus_context = modbus_new_tcp("0.0.0.0", 1502);
					if (!modbus_context)
					{
						std::cerr << "Error creating a libmodbus TCP interface\n";
						return 1;
					}
				}
#endif
				modbus_interface.resume();
			}
			if ( !(items[1].revents & ZMQ_POLLIN) ) {
				usleep(50);
				continue;
			}

			zmq::message_t update;
			if (!subscription_manager.subscriber().recv(&update, ZMQ_DONTWAIT)) {
				if (errno == EAGAIN) { usleep(50); continue; }
				if (errno == EFSM) exit(1);
				if (errno == ENOTSOCK) exit(1);
				std::cerr << "subscriber recv: " << zmq_strerror(zmq_errno()) << "\n";
				if (++error_count > 5) exit(1);
				continue;
			}
			error_count = 0;

			long len = update.size();
			char *data = (char *)malloc(len+1);
			memcpy(data, update.data(), len);
			data[len] = 0;
			if (DEBUG_BASIC) std::cout << "received: "<<data<<" from clockwork\n";

			std::vector<Value> params(0);
			size_t count = parseIncomingMessage(data, params);
			std::string cmd(params[0].asString());
			free(data);
			data = 0;

			if (cmd == "UPDATE" && count>= 6)
			{
				int group, addr, len;
				std::string name;

				assert(params.size() >= 6);
				assert(params[1].kind == Value::t_integer);
				assert(params[2].kind == Value::t_integer);
				assert(params[3].kind == Value::t_string);
				assert(params[4].kind == Value::t_integer);

				group = (int)params[1].iValue;
				addr = (int)params[2].iValue;
				name = params[3].sValue;
				len = (int)params[4].iValue;

				if (debug) std::cout << "IOD: " << group << " " << addr << " " << name << " " << len << " " << params[5] <<  "\n";
				if (params[5].kind == Value::t_string) {
					std::string valstr = params[5].asString();
					insert(group, addr-1, valstr.c_str(), valstr.length()+1);
				}
				else
					insert(group, addr-1, (int)params[5].iValue, len);
			}
			else if (cmd == "STARTUP")
			{
#if 1
				active_addresses.clear();
				initialised_address.clear();
				exit(0);
				break;
#else
				char *initial_settings = g_iodcmd->sendMessage("MODBUS REFRESH");
				loadData(initial_settings);
				free(initial_settings);
#endif
			}
			else if (cmd == "DEBUG")
			{
				assert(params.size() >= 2);
				assert(params[1].kind == Value::t_symbol);
				debug =  (params[1].sValue == "ON") ? true : false;
			}
		}
	}
	catch (std::exception& e)
	{
		if (zmq_errno())
			std::cerr << "ZMQ Error: " << zmq_strerror(zmq_errno()) << "\n";
		else
			std::cerr <<  "Exception : " <<  e.what() << "\n";
		return 1;
	}
	catch (...)
	{
		std::cerr << "Exception of unknown type!\n";
		finish(SIGTERM);
		exit(1);
	}

	//modbus_interface.pause(); // terminate modbus connections
	modbus_interface.stop(); // may hang if clients are connected
	monitor_modbus.join();
	return 0;
}

