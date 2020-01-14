#ifndef __modbus_helpers_h__
#define __modbus_helpers_h__

#include <set>

class SerialSettings {
public:
	unsigned int baud;
	unsigned int bits;
	char parity;
	unsigned int stop_bits;
    SerialSettings(){}
    SerialSettings(unsigned int rate, unsigned int width, char p, unsigned int stop)
      : baud(rate), bits(width), parity(p), stop_bits(stop) {}
private:
    SerialSettings(const SerialSettings &);
    SerialSettings &operator=(const SerialSettings &);
};

enum ModbusType {mt_unknown, mt_TCP, mt_RTU, mt_ASCII};

struct ModbusSettings {
	ModbusType mt;
	bool support_single_register_write;
	bool support_multi_register_write;
	std::string device_name; // name of the serial bus or tcp host name
	std::string settings; // serial port settings string or port name
	SerialSettings serial; // decoded version of serial port settings
    std::set<int> devices; // there can be several devices on one serial bus
};

int getSettings(const char *str, SerialSettings &settings);

#if 0
class ModbusService {
public:
	ModbusService(ModbusType kind, const SymbolTable &props) {
		SymbolTableConstIterator iter = props.begin();
		while (iter != props.end()) {
			properties.push_back(*iter++);
		}
	}
private:
	ModbusType mt;
	SymbolTable properties;
};
#endif

#endif
