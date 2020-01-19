#include <stdlib.h>
#include <string.h>
#include <string>
#include <iostream>
#include "options.h"
#include "modbus_helpers.h"

bool strToInt(const char *str, int &val) {
	char *q;
	long p = strtol(str, &q,10);
	if (q != str) {
		val = (int)p;
		return true;
	}
	return false;
}

bool strToLong(const char *str, long &val) {
	char *q;
	long p = strtol(str, &q,10);
	if (q != str) {
		val = p;
		return true;
	}
	return false;
}


void Options::usage(const char *prog) {
	std::cout << prog << " [-h hostname] [ -p port] [ -c modbus_config ] [ --channel channel_name ] "
		<< "[ --monitor clockwork_machine.property ] "
	    << "[ --tty tty_device ] [ --tty_settings settings ] [ --rtu ] "
        << "[ --device_id device_address ] [ --timeout timout-usecs ]\n\n"
		<< "  defaults to -h localhost -p 1502 --channel PLC_MONITOR\n"
		<< "  modbus rtu options: settings is a colon-separated specification, eg: 19200:8:N:1\n"
		<< "   -rtu if specified simply sets rtu mode with hard-coded defaults "
	    << "\n\n";
	std::cout << "  only one of the modbus_config or the channel_name should be supplied.\n";
	std::cout << "  setting hostname or port activates tcp mode, setting an rtu setting avtivates rtu mode\n";
	std::cout << "\n  other optional parameters:\n\n\t-s\tsimfile\t to create a clockwork configuration for simulation\n";
}

Options::Options() : verbose(false), multireg_write(true), singlereg_write(true), mt(mt_unknown),
        settings_tcp(0), settings_rtu(0), channel_name("PLC_MONITOR"), config_filename(""),
        valid_options(false) {
    settings_rtu = new ModbusSettings;
    settings_tcp = new ModbusSettings;

	settings_tcp->mt = mt_TCP;
	settings_tcp->device_name = "127.0.0.1";
	settings_tcp->settings = "1502";
	settings_tcp->support_single_register_write = true;
	settings_tcp->support_multi_register_write = true;

	settings_rtu->mt = mt_RTU;
	settings_rtu->device_name = "/dev/ttyUSB0";
	settings_rtu->settings = "19200:8:N:1";
	settings_rtu->support_single_register_write = true;
	settings_rtu->support_multi_register_write = true;

    serial_ = new SerialSettings;
	getSettings(settings_rtu->settings.c_str(), *serial_);

}

Options::~Options() {
    delete settings_tcp;
    delete settings_rtu;
}

const char *Options::channelName() {
    return channel_name.c_str();
}

const ModbusSettings *Options::settings() {
    if (mt == mt_TCP) return settings_tcp;
    if (mt == mt_RTU) return settings_rtu;
    return nullptr;
}

const SerialSettings *Options::serial(){
    return serial_;
}

bool Options::valid() {
    return valid_options;
}

const char *Options::configFileName() {
    return config_filename;
}

const char *Options::simulatorName() {
    return sim_name;
}

uint64_t Options::getTimeout() {
    return timeout_secs * 1000000 + timeout_usecs;
}

uint64_t Options::setTimeout(uint64_t timeout) {
    uint64_t res = getTimeout();
    timeout_secs = timeout / 1000000;
    timeout_usecs = timeout % 1000000;
    return res;
}

bool Options::parseArgs(int argc, const char *argv[]) {

	config_filename = 0;
	const char *sim_name = 0;

	int arg = 1;
	while (arg<argc) {
		if ( strcmp(argv[arg], "-h") == 0 && arg+1 < argc) {
			settings_tcp->device_name = argv[++arg];
			mt = mt_TCP;
		}
		else if ( strcmp(argv[arg], "-p") == 0 && arg+1 < argc) {
			settings_tcp->settings = argv[++arg];
			mt = mt_TCP;
		}
		else if ( strcmp(argv[arg], "-c") == 0 && arg+1 < argc) {
			config_filename = argv[++arg];
		}
		else if ( strcmp(argv[arg], "--channel") == 0 && arg+1 < argc) {
			channel_name = argv[++arg];
		}
		else if ( strcmp(argv[arg], "-s") == 0 && arg+1 < argc) {
			sim_name = argv[++arg];
		}
		else if ( strcmp(argv[arg], "--no-multireg-write") == 0) {
			multireg_write = false;
		}
		else if ( strcmp(argv[arg], "--no-singlereg-write") == 0) {
			singlereg_write = false;
		}
		else if ( strcmp(argv[arg], "-v") == 0) {
			verbose = true;
		}
		else if ( strcmp(argv[arg], "--monitor") == 0 && arg+1 < argc) {
			std::string mon = argv[++arg];
			status_machine = mon;
			size_t pos = mon.find_last_of(".");
			if (pos) {
				status_property=mon.substr(pos+1);
				status_machine.erase(pos);;
			}
			else status_property = "status";
			if (verbose) std::cout << "reporting status to property " << status_property << " of " << status_machine << "\n";
		}
		else if ( strcmp(argv[arg], "--tty") == 0 && arg+1 < argc) {
			settings_rtu->device_name = argv[++arg];
			mt = mt_RTU;
		}
		else if ( strcmp(argv[arg], "--tty_settings") == 0 && arg+1 < argc) {
			settings_rtu->settings = argv[++arg];
			mt = mt_RTU;
			int res = getSettings(settings_rtu->settings.c_str(), settings_rtu->serial);
			if (res == 0) {
				if (verbose) std::cout << "tty settings: baud: " << settings_rtu->serial.baud 
				  << " bits: " << settings_rtu->serial.bits << "\n";
			}
			else {
				std::cerr << "failed to parse settings: " << settings_rtu->settings.c_str() << "\n";
				exit(1);
			}
		}
		else if ( strcmp(argv[arg], "--rtu") == 0) {
			mt = mt_RTU;
		}
		else if ( strcmp(argv[arg], "--device_id") == 0 && arg+1 < argc) {
			int device_id;
			if (strToInt(argv[++arg], device_id)) {
				settings_rtu->devices.insert(device_id);
				mt = mt_RTU;
			}
		}
		else if ( strcmp(argv[arg], "--timeout") == 0 && arg+1 < argc) {
			long timeout;
			if (strToLong(argv[++arg], timeout)) {
				setTimeout(timeout);
			}
		}

		else if (argv[arg][0] == '-'){
			std::cerr << "unknown option " << argv[arg] << "\n";
			usage(argv[0]);
			exit(0); 
		}
		else break;
		++arg;
	}
    settings_rtu->support_single_register_write = singlereg_write;
    settings_rtu->support_multi_register_write = multireg_write;
    settings_tcp->support_single_register_write = singlereg_write;
	settings_tcp->support_multi_register_write = multireg_write;
    valid_options = true;
    return true;
}