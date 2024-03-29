#include <iostream>
#include <zmq.hpp>
#include <modbus.h>
#include <unistd.h>
#include <stdlib.h>
#include <boost/thread.hpp>
#include <map>
#include <set>
#include "modbus_helpers.h"
#include "buffer_monitor.h"
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
#include "options.h"

bool iod_connected = false;
bool update_status = true;
//const char *program_name;

/*
void getTimeString(char *buf, size_t buf_size) {
	struct timeval now_tv;
	gettimeofday(&now_tv,0);
	struct tm now_tm;
	localtime_r(&now_tv.tv_sec, &now_tm);
	uint32_t msec = now_tv.tv_usec / 1000L;
	snprintf(buf, 50,"%04d-%02d-%02d %02d:%02d:%02d.%03d ",
			 now_tm.tm_year+1900, now_tm.tm_mon+1, now_tm.tm_mday,
			 now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec, msec);
}
*/

struct UserData {
	
};

Options options;

/* Clockwork interface */

void sendMessage(zmq::socket_t &socket, const char *message) {
	safeSend(socket, message, strlen(message));
}

std::list<Value> params;
char *send_command(zmq::socket_t &sock, std::list<Value> &params) {
	if (params.size() == 0) return 0;
	Value cmd_val = params.front();
	params.pop_front();
	std::string cmd = cmd_val.asString();
	char *msg = MessageEncoding::encodeCommand(cmd, &params);
	if (options.verbose) std::cerr << " sending: " << msg << "\n";
	sendMessage(sock, msg);
	size_t size = strlen(msg);
	free(msg);
	zmq::message_t reply;
	if (sock.recv(&reply)) {
		size = reply.size();
		char *data = (char *)malloc(size+1);
		memcpy(data, reply.data(), size);
		data[size] = 0;

		return data;
	}
	return 0;
}

void process_command(zmq::socket_t &sock, std::list<Value> &params) {
	char * data = send_command(sock, params);
	if (data) {
		if (options.verbose) std::cerr << data << "\n";
		free(data);
	}
}

void sendStatus(const char *s) {
	zmq::socket_t sock(*MessagingInterface::getContext(), ZMQ_REQ);
	sock.connect("tcp://localhost:5555");

	{FileLogger fl(program_name); fl.f() << "reporting status " << s << "\n"; }
	if (options.status_machine.length()) {
		std::list<Value>cmd;
		cmd.push_back("PROPERTY");
		cmd.push_back(options.status_machine.c_str());
		cmd.push_back(options.status_property.c_str());
		cmd.push_back(s);
		process_command(sock, cmd);
	}
	else
		{FileLogger fl(program_name); fl.f() << "no status machine" << "\n"; }
}

/*
#if 0
class ClockworkClientThread {

public:
	ClockworkClientThread(): finished(false) { 
		int linger = 0; // do not wait at socket close time

		socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
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
*/


std::map<int, UserData *> active_addresses;
std::map<int, UserData *> ro_bits;
std::map<int, UserData *> inputs;
//std::map<int, UserData *> registers;

void sendStateUpdate(zmq::socket_t *sock, ModbusMonitor *mm, bool which) {
	std::list<Value> cmd;
	cmd.push_back("SET");
	cmd.push_back(mm->name().c_str());
	cmd.push_back("TO");
	if (which) cmd.push_back("on"); else cmd.push_back("off");
	process_command(*sock, cmd);
}

void sendPropertyUpdate(zmq::socket_t *sock, ModbusMonitor *mm) {
	std::list<Value> cmd;
	long value = 0;
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
	// note: the monitor address is in the global range grp<<16 + offset
	// this method is only using the addresses in the local range
	//mm->set(buffer_addr + ( (mm->address() & 0xffff)) );
	if (mm->length() == 1 && mm->format() == "SignedInt") {
		value = *( (int16_t*)mm->value->getWordData() );
		cmd.push_back( value );
	}
	else if (mm->length() == 1) {
		value = *( (uint16_t*)mm->value->getWordData() );
		cmd.push_back( value );
	}
	else if (mm->length() == 2 && mm->format() == "Float") {
		uint16_t *xx = (uint16_t*)mm->value->getWordData();
		Value vv(*((float*)xx));
		cmd.push_back( *( (float*)xx ));
	}
	else if (mm->length() == 2 && mm->format() == "SignedInt") {
		uint16_t modbus_value[2];
		modbus_value[0] = (mm->value->getWordData())[0];
		modbus_value[1] = (mm->value->getWordData())[1];
		value = MODBUS_GET_INT32_FROM_INT16(modbus_value, 0);
		cmd.push_back( (int32_t)value );
	}
	else if (mm->length() == 2) {
		uint16_t modbus_value[2];
		modbus_value[0] = (mm->value->getWordData())[0];
		modbus_value[1] = (mm->value->getWordData())[1];
		value = MODBUS_GET_INT32_FROM_INT16(modbus_value, 0);
		cmd.push_back( (uint32_t)value );
	}
	else {
		cmd.push_back(0); // TBD
	}
	process_command(*sock, cmd);

}

void displayChanges(zmq::socket_t *sock, std::set<ModbusMonitor*> &changes, uint8_t *buffer_addr) {
	if (changes.size()) {
		if (options.verbose) std::cerr << changes.size() << " changes\n";
		std::set<ModbusMonitor*>::iterator iter = changes.begin();
		while (iter != changes.end()) {
			ModbusMonitor *mm = *iter++;
			// note: the monitor address is in the global range grp<<16 + offset
			// this method is only using the addresses in the local range
			uint8_t *val = buffer_addr + ( (mm->address() & 0xffff));
			if (options.verbose) std::cerr << mm->name() << " ";
			mm->set( val, options.verbose );
			
			if (sock &&	(mm->group() == 0 || mm->group() == 1) && mm->length()==1) {
				sendStateUpdate(sock, mm, (bool)*val);
			}
		}
	}
}

void displayChanges(zmq::socket_t *sock, std::set<ModbusMonitor*> &changes, uint16_t *buffer_addr) {
	if (changes.size()) {
		if (options.verbose) std::cerr << changes.size() << " changes\n";
		std::set<ModbusMonitor*>::iterator iter = changes.begin();
		while (iter != changes.end()) {
			ModbusMonitor *mm = *iter++;
			uint16_t *val = buffer_addr + ( (mm->address() & 0xffff)) ;
			if (options.verbose) std::cerr << mm->name() << " ";
			mm->set( val, options.verbose );
			if (mm->readOnly() && sock) { // INPUTREGISTER
				sendPropertyUpdate(sock, mm);
			}
		}
	}
}

#include "modbus_client_thread.cpp"
ModbusClientThread *mb = 0;

class SetupDisconnectMonitor : public EventResponder {
public:
	void operator()(const zmq_event_t &event_, const char* addr_) {
		iod_connected = false;
		exit(0);
	}
};

static bool need_refresh = false;

class SetupConnectMonitor : public EventResponder {
public:
	void operator()(const zmq_event_t &event_, const char* addr_) {
		iod_connected = true;
		need_refresh = true;
	}
};

void loadRemoteConfiguration(zmq::socket_t &iod, std::string &chn_instance_name, PLCInterface &plc, MonitorConfiguration &mc) {


	std::list<Value>cmd;
	cmd.push_back("REFRESH");
	cmd.push_back(chn_instance_name.c_str());
	cJSON *obj = nullptr;
	{
		char *response = send_command(iod, cmd);
		if (options.verbose) std::cerr << response << "\n";
		obj = cJSON_Parse(response);
		free(response);
	}

	if (obj) {
		iod_connected = true;
		if (obj->type != cJSON_Array) {
			std::cerr << "error. clock response is not an array";
			char *item = cJSON_Print(obj);
			std::cerr << item << "\n";
      free(item);
		}
		cJSON *item = obj->child;
		while (item) {
			cJSON *name_js = cJSON_GetObjectItem(item, "name");
			cJSON *addr_js = cJSON_GetObjectItem(item, "address");
			cJSON *length_js = cJSON_GetObjectItem(item, "length");
			cJSON *type_js = cJSON_GetObjectItem(item, "type");
			cJSON *format_js = cJSON_GetObjectItem(item, "format");

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
			bool readonly = true;
			if (type == "INPUTBIT") group = 1;
			else if (type == "OUTPUTBIT") { group = 0; readonly = false; }
			else if (type == "INPUTREGISTER") group = 3;
			else if (type == "OUTPUTREGISTER") { group = 4; readonly = false; }

			std::string format;
			if (format_js && type_js->type == cJSON_String) {
				format = format_js->valuestring;
			}
			else if (group == 1 || group == 0) format = "BIT";
			else if (group == 3 || group == 4) format = "SignedInt";
			else format = "WORD";

			int addr = 0;
			std::string addr_str;
			if (addr_js && addr_js->type == cJSON_String) {
				addr_str = addr_js->valuestring;
				std::pair<int, int> plc_addr = plc.decode(addr_str.c_str());
				if (plc_addr.first >= 0) group = plc_addr.first;
				if (plc_addr.second >= 0) addr = plc_addr.second;
			}
			ModbusMonitor *mm = new ModbusMonitor(name, group, addr, length, format, readonly);
			mc.monitors.insert(std::make_pair(name, *mm) );

			mm->add();
			item = item->next;
		}
		cJSON_Delete(obj);
	}
}

void setupMonitoring(MonitorConfiguration &mc) {

	std::map<std::string, ModbusMonitor>::const_iterator iter = mc.monitors.begin();
	while (iter != mc.monitors.end()) {
		const std::pair<std::string, ModbusMonitor> &item = *iter++;
		if (item.second.group() == 0) {
			for (unsigned int i=0; i<item.second.length(); ++i) {
				if (options.verbose)
					std::cerr << "monitoring: " << item.second.group() << ":"
					<< (item.second.address()+i) <<" " << item.second.name() << "\n";
				active_addresses[item.second.address()+i] = 0;
			}
		}
		else if (item.second.group() == 1) {
			for (unsigned int i=0; i<item.second.length(); ++i) {
				if (options.verbose)
					std::cerr << "monitoring: " << item.second.group()
					<< ":" << (item.second.address()+i) <<" " << item.second.name() << "\n";
				ro_bits[item.second.address()+i] = 0;
			}
		}
		else if (item.second.group() >= 3) {
			for (unsigned int i=0; i<item.second.length(); ++i) {
				if (options.verbose)
					std::cerr << "monitoring: " << item.second.group() << ":"
					<< (item.second.address()+i) <<" " << item.second.name() << "\n";
				inputs[item.second.address()+i] = 0;
			}
		}
	}

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
				const Value &v	= *iter++;
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
			if (options.verbose) std::cerr << ds << "\n";
			parts.push_back(ds.c_str());
			++count;
		}
		std::copy(parts.begin(), parts.end(), std::back_inserter(params));
	}
	return count;
}


using namespace std;
int main(int argc, const char *argv[]) {
	program_name = strdup(basename((char*)argv[0]));
	zmq::context_t context;
	MessagingInterface::setContext(&context);

	std::cerr << "Modbus version (compile time): " << LIBMODBUS_VERSION_STRING << " ";
	std::cerr << "(linked): " 
			<< libmodbus_version_major << "." 
			<< libmodbus_version_minor << "." << libmodbus_version_micro << "\n";

	if (!options.parseArgs(argc, argv)) {
		options.usage(program_name);
	}
	const ModbusSettings *ms = options.settings();
  if (!ms) { exit(1); }

	PLCInterface plc;
	if (!plc.load("modbus_addressing.conf")) {
		std::cerr << "Failed to load plc mapping configuration\n";
		exit(1);
	}

	{FileLogger fl(program_name); fl.f() << "----- starting -----\n"; }
	std::string chn_instance_name;
	MonitorConfiguration mc;
	if (options.configFileName()) {
		if (!mc.load(options.configFileName())) {
			cerr << "Failed to load modbus mappings to be monitored\n";
			exit(1);
		}
	}
	else {

		int linger = 0; // do not wait at socket close time
		zmq::socket_t iod(*MessagingInterface::getContext(), ZMQ_REQ);
		iod.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
		iod.connect("tcp://localhost:5555");

		std::list<Value>cmd;
		cmd.push_back("CHANNEL");
		cmd.push_back(options.channelName());
		cJSON *obj = nullptr;
		{
			char *response = send_command(iod, cmd);
			if ( !response ) {
				FileLogger fl(program_name); fl.f() << "null response to channel request. exiting\n"; sleep(2); exit(1);
			}
			else if (!*response) {
				FileLogger fl(program_name); fl.f() << "empty response to channel request. exiting\n"; sleep(2); exit(2);
			}
			if (options.verbose) std::cerr << response << "\n";
			obj = cJSON_Parse(response);

			free(response);
		}
		
		if (obj) {
			cJSON *name_js = cJSON_GetObjectItem(obj, "name");
			if (name_js) {
				chn_instance_name = name_js->valuestring;
			}
			else {
				char *resp_str = cJSON_PrintUnformatted(obj);
				std::cerr << "configuration error, expected to find a field 'name' in " << resp_str << "\n";
				free(resp_str);
			}
			cJSON_Delete(obj);
			obj = 0;
			if (!name_js) {
				exit(3);
			}
		}
		else {
		}
		if (options.verbose) std::cerr << chn_instance_name << "\n";

		sendStatus("initialising");
		update_status = true;
		loadRemoteConfiguration(iod, chn_instance_name, plc, mc);
	}

	if (options.simulatorName()) {
		mc.createSimulator(options.simulatorName());
		exit(0);
	}

	setupMonitoring(mc);

	if (options.configFileName()) {
		// standalone execution

		ModbusClientThread modbus_interface(*ms, mc);
		mb = &modbus_interface;
		boost::thread monitor_modbus(boost::ref(modbus_interface));

		while (true) {
			usleep(100000);
		}
		exit(0);
	}

	// running along with clockwork

	// the local command channel accepts commands from the modbus thread and relays them to iod.
	const char *local_commands = "inproc://local_cmds";
	
	zmq::socket_t iosh_cmd(*MessagingInterface::getContext(), ZMQ_REP);
	iosh_cmd.bind(local_commands);

	SubscriptionManager subscription_manager(chn_instance_name.c_str(), eCLOCKWORK, "localhost", 5555);
	SetupDisconnectMonitor disconnect_responder;
	SetupConnectMonitor connect_responder;
	subscription_manager.monit_setup->addResponder(ZMQ_EVENT_DISCONNECTED, &disconnect_responder);
	subscription_manager.monit_setup->addResponder(ZMQ_EVENT_CONNECTED, &connect_responder);
	subscription_manager.setupConnections();

	ModbusClientThread modbus_interface(*ms, mc, local_commands);
	mb = &modbus_interface;
	boost::thread monitor_modbus(boost::ref(modbus_interface));

	enum ProgramState {
		s_initialising,
		s_running, // polling for io changes from clockwork
		s_finished,// about to exit
	} program_state = s_initialising;
	
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
				if (options.verbose) std::cerr << "no connection to iod\n";
				usleep(1000000);
				exception_count = 0;
				continue;
			}

			exception_count = 0;
		}
		catch (std::exception ex) {
			std::cerr << "polling connections: " << ex.what() << "\n";
			{FileLogger fl(program_name); fl.f() << "exception when polling connections " << ex.what()<< "\n"; }
			if (++exception_count <= 5 && program_state != s_finished) { usleep(400000); continue; }
			exit(0);
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
		std::cerr << "received: " << data << " (len == " <<len << " from clockwork\n";

		std::vector<Value> params(0);
		parseIncomingMessage(data, params);
		free(data);
		data = 0;
		std::string cmd = "Unknown";
		if (params.size() > 0) {
			cmd = params[0].asString();
		}
		else {
			std::cerr << "unexpected data received\n";
		}

		if (cmd == "STATE") {
			try {
				ModbusMonitor &m = mc.monitors.at(params[1].asString());
				if (options.verbose) std::cerr << m.name() << " " << ( (m.readOnly()) ? "READONLY" : "" ) << "\n";
				if (!m.readOnly()) {
					if (params[2].asString() == "on")
						mb->requestUpdate(m.address(), true);
					else
						mb->requestUpdate(m.address(), false);
				}
				//sendStateUpdate(&iosh_cmd, &m, *(m.value->getWordData()) );
			}
			catch (std::exception ex) {
				std::cerr << "Exception when processing STATE command: " << ex.what() << "\n";
			}
		}
		else if (cmd == "PROPERTY" && params.size() >= 4) {
			try {
				std::map<std::string, ModbusMonitor>::iterator found = mc.monitors.find(params[1].asString());
				if (found == mc.monitors.end()) {
					std::cerr << "Error: not monitoring property " << params[1] << "\n";
					continue;
				}
				ModbusMonitor &m = (*found).second; //mc.monitors.at(params[0].asString());
				if (options.verbose) std::cerr << m.name() << " " << ( (m.readOnly()) ? "READONLY" : "" ) << "\n";
				if (!m.readOnly() && m.group() == 4) {
					if (options.verbose) std::cerr << "setting " << m.name() << " (" << m.format() << ")\n";
					if (params[2].asString() == "VALUE") {
						if (m.format() == "Float") {
							double dval;
							if (params[3].asFloat(dval)) {
								uint16_t value[2];
								float fval = dval;
								if (options.verbose)
									std::cerr << "setting float value " << fval << " for address"
										<< std::hex << "0x" << m.address() << std::dec << "\n";
								modbus_set_float_badc(fval, value);
								if (m.length() == 2) {
									m.setRaw(value, 2);
									mb->requestRegisterUpdates(m.address(), value, 2);
								}
								else {
									std::cerr << "Error: cannot set float value for " << params[1] << " into a field of length " << m.length() << "\n";
								}
							}
							else {
								std::cerr << "Error: could not convert '" << params[3] << "' to a float\n";
							}
						}
						else {
							long value;
							if (params[2].asString() == "VALUE" && params[3].asInteger(value)) {
								if (m.length() == 1) {
									m.setRaw( (uint16_t) value);
									mb->requestRegisterUpdate(m.address(), (uint16_t)value);
								}
								else if (m.length() == 2) {
									uint16_t modbus_value[2];
									MODBUS_SET_INT32_TO_INT16(modbus_value, 0, (uint32_t)(value & 0xffffffff));
									m.setRaw( modbus_value ,2 );
									mb->requestRegisterUpdates(m.address(), (uint16_t*)&modbus_value, 2);
								}
							}
						}
					}
				}
				//sendPropertyUpdate(&iosh_cmd, &m);	// dont' send the property value back
			}
			catch (std::exception ex) {
				std::cerr << "Exception when processing PROPERTY command: " << ex.what() << "\n";
			}
		}
	}

}
