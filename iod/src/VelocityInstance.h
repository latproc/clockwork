#ifndef __VELOCITYINSTANCE_H__
#define __VELOCITYINSTANCE_H__

#include "MachineInstance.h"
#include "VelocityEstimator.h"
#include <list>

class VelocityInstance : public MachineInstance {
  protected:
    VelocityInstance(InstanceType instance_type = MACHINE_INSTANCE);
    VelocityInstance(CStringHolder name, const char *type,
                     InstanceType instance_type = MACHINE_INSTANCE);

  public:
    ~VelocityInstance();
    static void sampleAll(uint64_t sample_clock);
    static uint64_t minSamplePeriodMs();

  private:
    VelocityInstance &operator=(const VelocityInstance &orig);
    VelocityInstance(const VelocityInstance &other);

    void sampleSource(uint64_t sample_clock);
    void configureEstimator();
    void publishInternal(const VelocityEstimator::Result &result);

    VelocityEstimator estimator_;
    uint64_t last_source_time_;
    uint64_t sample_period_us_;

    static std::list<VelocityInstance *> instances;

    friend class MachineInstanceFactory;
};

#endif
