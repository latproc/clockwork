#ifndef __PLC_INTERFACE__
#define __PLC_INTERFACE__

#include <ostream>
#include <string>
#include <map>

class PLCMapping {
public:
    PLCMapping(const char *code, unsigned int start, unsigned int end, unsigned int group, unsigned int address);
    PLCMapping(const PLCMapping &orig);
    PLCMapping &operator=(const PLCMapping &other);
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const PLCMapping &other);

	unsigned int &address() { return address_; }
	const unsigned int &address() const { return address_; }
	
	unsigned int &group() { return group_; }
	const unsigned int &group() const { return group_; }
	//void address(int a) { address_ = a; }
    
private:
    std::string code_;
	unsigned int start_;
	unsigned int end_;
	unsigned int group_;
	unsigned int address_;
};

std::ostream &operator<<(std::ostream &out, const PLCMapping &m);

class PLCInterface {
public:
	std::map<std::string, PLCMapping> mappings;
	
	bool load(const char *fname);
	std::pair<int, int> decode(const char *address);
};

#endif
