#ifndef __modbus_helpers_h__
#define __modbus_helpers_h__

struct SerialSettings {
	unsigned int baud;
	char parity;
	char bits;
	char stop_bits;
};

enum ModbusType {mt_TCP, mt_RTU, mt_ASCII};

struct ModbusSettings {
	ModbusType mt;
	std::string device_name;
	std::string settings;
	SerialSettings serial;
};

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
