#ifndef __PARAMETER_H__
#define __PARAMETER_H__

#include "symboltable.h"
#include "value.h"
#include <string>

class MachineInstance;

class Parameter {
  public:
    Value val;
    SymbolTable properties;
    MachineInstance *machine;
    std::string real_name;
    Parameter(Value v);
    Parameter(const char *name, const SymbolTable &st);
    std::ostream &operator<<(std::ostream &out) const;
    Parameter(const Parameter &orig);
};
std::ostream &operator<<(std::ostream &out, const Parameter &p);

#endif
