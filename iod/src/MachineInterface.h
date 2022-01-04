#ifndef __MACHINEINTERFACE_H__
#define __MACHINEINTERFACE_H__

#include "MachineClass.h"
#include <map>

class MachineInterface : public MachineClass {
  public:
    MachineInterface(const char *class_name);
    virtual ~MachineInterface();
    static std::map<std::string, MachineInterface *> all_interfaces;
    //  void addProperty(const char *name);
    //  void addCommand(const char *name);
};

#endif
