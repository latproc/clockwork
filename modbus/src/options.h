#ifndef __MB_OPTIONS_H__
#define __MB_OPTIONS_H__

#include <string>

enum ModbusType {mt_unknown, mt_TCP, mt_RTU, mt_ASCII};

bool strToInt(const char *str, int &val);

struct ModbusSettings;
class SerialSettings;

class Options {
public:
	bool verbose;
	bool multireg_write; // some devices do not support the multiple register write function
	bool singlereg_write; // some devices do not support writing single registers at a time 
	std::string status_machine;
	std::string status_property;
	Options();
	~Options();
	bool parseArgs(int argc, const char *argv[]); // false if the options are invalid
	bool valid(); // are the user selected options valid?
	void usage(const char *prog);
	const ModbusSettings *settings();
	const SerialSettings *serial();
	const char *channelName();
    const char *configFileName();
    const char *simulatorName();
    uint64_t getTimeout();
    uint64_t setTimeout(uint64_t timeout_usec);

private:
    ModbusType mt;
	ModbusSettings *settings_tcp;
	ModbusSettings *settings_rtu;
	SerialSettings *serial_;
	std::string channel_name;
    const char *config_filename;
	bool valid_options;
    const char *sim_name;
    uint32_t timeout_secs;
    uint32_t timeout_usecs;
};

#endif
