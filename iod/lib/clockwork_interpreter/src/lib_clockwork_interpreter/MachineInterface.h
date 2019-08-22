#ifndef __MACHINEINTERFACE_H__
#define __MACHINEINTERFACE_H__

#include <map>
#include "MachineClass.h"

class MachineInterface : public MachineClass {
public:
    MachineInterface(const char *class_name);
    virtual ~MachineInterface();
    static std::map<std::string, MachineInterface *> all_interfaces;
//	void addProperty(const char *name);
//	void addCommand(const char *name);
};

#endif
