#ifndef __RATEESTIMATORINSTANCE_H__
#define __RATEESTIMATORINSTANCE_H__

#include "MachineInstance.h"

class CounterRateFilterSettings;
class RateEstimatorInstance : public MachineInstance {
  protected:
    RateEstimatorInstance(InstanceType instance_type = MACHINE_INSTANCE);
    RateEstimatorInstance(CStringHolder name, const char *type,
                          InstanceType instance_type = MACHINE_INSTANCE);

  public:
    ~RateEstimatorInstance();
    virtual bool setValue(const std::string &property, const Value &new_value,
                          uint64_t authority = 0) override;
    int64_t filter(int64_t val) override;
    virtual void setNeedsCheck() override;
    virtual void idle() override;
    //virtual bool hasWork();
    CounterRateFilterSettings *getSettings() { return settings; }

  private:
    RateEstimatorInstance &operator=(const RateEstimatorInstance &orig);
    RateEstimatorInstance(const RateEstimatorInstance &other);
    CounterRateFilterSettings *settings;

    friend class MachineInstanceFactory;
};

#endif
