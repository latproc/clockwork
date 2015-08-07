//
// monitor.h
// Created by Martin on 4/08/2015.

#ifndef _monitor_h_
#define _monitor_h_

#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <string.h>

//void display(uint8_t *p, size_t len);
//void displayAscii(uint8_t *p, size_t len);

template<class T>void display(T *p, size_t len) {
	size_t min = 0;
	for (size_t i=min; i<len; ++i) 
		std::cout << std::setw(2*sizeof(T)) << std::setfill('0') << std::hex << (unsigned int)p[i] << std::dec;
}

template<class T>void displayAscii(T *p, size_t len) {
	size_t min = 0;
	for (size_t i=min; i<len; ++i) 
	std::cout << std::setw(2*sizeof(T)) << std::setfill('0') << std::hex << (unsigned int)p[i] << std::dec;
	std::cout << "\n";
	for (size_t i=min; i<len; ++i) {
		uint8_t buf[sizeof(T)];
		memcpy(buf, p+i, sizeof(T));

		for (size_t j=0; j<sizeof(T); ++j)  {
			std::cout << '.' << ( (isprint(buf[j]) ? (char)buf[j] : '.') );
		}
	}
	std::cout << "\n";
}

class  ModbusValue { 
public:
	virtual ~ModbusValue();
	ModbusValue(unsigned int len);
	virtual void set(uint8_t *data) = 0;
	virtual void set(uint16_t *data) = 0;
	virtual uint8_t *getBitData() = 0;
	virtual uint16_t *getWordData() = 0;

	unsigned int length;
private:
	ModbusValue(const ModbusValue &other);
	ModbusValue &operator=(const ModbusValue &other);
};

class ModbusValueBit : public ModbusValue {
public:
	uint8_t *val;
	ModbusValueBit(unsigned int len) :ModbusValue(len), val(0) { val = new uint8_t[len]; }
	~ModbusValueBit() { delete[] val; }
	virtual void set(uint8_t *data);
	virtual void set(uint16_t *data);
	virtual uint8_t *getBitData() { return val; };
	virtual uint16_t *getWordData() { return 0; }
	
private:
	ModbusValueBit(const ModbusValueBit &other);
	ModbusValueBit &operator=(const ModbusValueBit &other);
};

class ModbusValueWord : public ModbusValue{
public:
	uint16_t *val;
	ModbusValueWord(unsigned int len) : ModbusValue(len), val(0) { val = new uint16_t[len]; }
	~ModbusValueWord() { delete[] val; }
	virtual void set(uint8_t *data);
	virtual void set(uint16_t *data);
	virtual uint8_t *getBitData() { return 0; };
	virtual uint16_t *getWordData() { return val; }
	private:
		ModbusValueWord(const ModbusValueWord &other);
		ModbusValueWord &operator=(const ModbusValueWord &other);
};


class ModbusMonitor {
	public:
	    ModbusMonitor(std::string name, unsigned int group, unsigned int address, unsigned int len);
	    ModbusMonitor(const ModbusMonitor &orig);
	    ModbusMonitor &operator=(const ModbusMonitor &other);
	    std::ostream &operator<<(std::ostream &out) const;
	    bool operator==(const ModbusMonitor &other);
	
		std::string &name() { return name_; }
		const std::string &name() const { return name_; }

		unsigned int &address() { return address_; }
		const unsigned int &address() const { return address_; }
		
		unsigned int &group() { return group_; }
		const unsigned int &group() const { return group_; }
		//void address(int a) { address_ = a; }
		unsigned int &length() { return len_; }
		const unsigned int &length() const { return len_; }
		
		void set(uint8_t *new_value, bool display = true);
		void set(uint16_t *new_value, bool display = true);		
		
		static ModbusMonitor *lookupAddress(unsigned int adr);
		static ModbusMonitor *lookup(unsigned int group, unsigned int adr);
		
		void add();
	    
	private:
		std::string name_;
		unsigned int group_;
		unsigned int address_;
		unsigned int len_;
		ModbusValue *value;
	protected:
		static std::map<unsigned int, ModbusMonitor*>addresses;
		
		friend class MonitorConfiguration;
	};

std::ostream &operator<<(std::ostream &out, const ModbusMonitor &m);

class MonitorConfiguration {
public:
	std::map<std::string, ModbusMonitor> monitors;
	bool load(const char *fname);
};


#endif