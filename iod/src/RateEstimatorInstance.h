#ifndef __RATEESTIMATORINSTANCE_H__
#define __RATEESTIMATORINSTANCE_H__

#include "MachineInstance.h"

class CounterRateFilterSettings;
class RateEstimatorInstance : public MachineInstance {
protected:
  RateEstimatorInstance(InstanceType instance_type = MACHINE_INSTANCE);
  RateEstimatorInstance(CStringHolder name, const char * type, InstanceType instance_type = MACHINE_INSTANCE);
public:
  ~RateEstimatorInstance();
  void setValue(const std::string &property, Value new_value, uint64_t authority =0);
  long filter(long val);
  virtual void setNeedsCheck();
  virtual void idle();
  //virtual bool hasWork();
  CounterRateFilterSettings *getSettings() { return settings; }
private:
  RateEstimatorInstance &operator=(const RateEstimatorInstance &orig);
  RateEstimatorInstance(const RateEstimatorInstance &other);
  CounterRateFilterSettings *settings;

  friend class MachineInstanceFactory;
};

#endif
