#ifndef __COUNTERRATEINSTANCE_H__
#define __COUNTERRATEINSTANCE_H__

#include "MachineInstance.h"

class CounterRateFilterSettings;
class CounterRateInstance : public MachineInstance {
protected:
  CounterRateInstance(InstanceType instance_type = MACHINE_INSTANCE);
  CounterRateInstance(CStringHolder name, const char * type, InstanceType instance_type = MACHINE_INSTANCE);
public:
  ~CounterRateInstance();
  void setValue(const std::string &property, Value new_value, uint64_t authority = 0);
  long filter(long val);
  virtual void idle();
  //virtual bool hasWork();
  CounterRateFilterSettings *getSettings() { return settings; }
private:
  CounterRateInstance &operator=(const CounterRateInstance &orig);
  CounterRateInstance(const CounterRateInstance &other);
  CounterRateFilterSettings *settings;

  friend class MachineInstanceFactory;
};

# endif
