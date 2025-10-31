#ifndef __modbus_helpers_h__
#define __modbus_helpers_h__

#include "options.h"
#include <iomanip>
#include <ostream>
#include <set>

// Modbus errors are based on MODBUS_ENOBASE. Display the standard error string
// if the code is less than MODBUS_ENOBASE, the modbus error string if the code
// is within the modbus range and otherwise display the error as a   'device
// error'
std::string show_modbus_error(int rc);

template <typename T> void display(std::ostream &out, T *p, size_t len) {
    size_t min = 0;
    if (len > 120)
        len = 120;
    for (size_t i = min; i < len; ++i) {
        out << std::setw(2 * sizeof(T)) << std::setfill('0') << std::hex << (unsigned int)p[i]
            << std::dec;
    }
}

template <typename T> void displayAscii(std::ostream &out, T *p, size_t len) {
    size_t min = 0;
    if (len > 120)
        len = 120;
    display(out, p, len);
    out << "\n";
    for (size_t i = min; i < len; ++i) {
        uint8_t buf[sizeof(T)];
        memcpy(buf, p + i, sizeof(T));

        for (size_t j = 0; j < sizeof(T); ++j) {
            out << '.' << ((isprint(buf[j]) ? (char)buf[j] : '.'));
        }
    }
    out << "\n";
}

class SerialSettings {
  public:
    int baud;
    int bits;
    char parity;
    int stop_bits;
    SerialSettings() {}
    SerialSettings(unsigned int rate, unsigned int width, char p, unsigned int stop)
        : baud(rate), bits(width), parity(p), stop_bits(stop) {}

  private:
    SerialSettings(const SerialSettings &);
    SerialSettings &operator=(const SerialSettings &);
};

struct ModbusSettings {
    ModbusType mt;
    bool support_single_register_write;
    bool support_multi_register_write;
    std::string device_name; // name of the serial bus or tcp host name
    std::string settings;    // serial port settings string or port name
    SerialSettings serial;   // decoded version of serial port settings
    std::set<int> devices;   // there can be several devices on one serial bus
    int device_id;           // TODO: Add support for multiple devices
};

int getSettings(const char *str, SerialSettings &settings);

bool isPrintable(const char *str);

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
