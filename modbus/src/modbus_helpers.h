#ifndef __modbus_helpers_h__
#define __modbus_helpers_h__

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
	std::string device_name;
	std::string settings;
	SerialSettings serial;
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
