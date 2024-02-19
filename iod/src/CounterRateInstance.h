#ifndef __COUNTERRATEINSTANCE_H__
#define __COUNTERRATEINSTANCE_H__

#include "MachineInstance.h"

class CounterRateFilterSettings;
class CounterRateInstance : public MachineInstance {
  protected:
    CounterRateInstance(InstanceType instance_type = MACHINE_INSTANCE);
    CounterRateInstance(CStringHolder name, const char *type,
                        InstanceType instance_type = MACHINE_INSTANCE);

  public:
    ~CounterRateInstance();
    virtual bool setValue(const std::string &property, const Value &new_value,
                          uint64_t authority = 0) override;
    int64_t filter(int64_t val) override;
    virtual void idle() override;
    //virtual bool hasWork();
    CounterRateFilterSettings *getSettings() { return settings; }

  private:
    CounterRateInstance &operator=(const CounterRateInstance &orig);
    CounterRateInstance(const CounterRateInstance &other);
    CounterRateFilterSettings *settings;

    friend class MachineInstanceFactory;
};

#endif
