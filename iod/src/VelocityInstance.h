#ifndef __VELOCITYINSTANCE_H__
#define __VELOCITYINSTANCE_H__

#include "MachineInstance.h"
#include "VelocityEstimator.h"

class VelocityInstance : public MachineInstance {
  protected:
    VelocityInstance(InstanceType instance_type = MACHINE_INSTANCE);
    VelocityInstance(CStringHolder name, const char *type,
                     InstanceType instance_type = MACHINE_INSTANCE);

  public:
    ~VelocityInstance();
    void idle() override;
    void setNeedsCheck() override;

  private:
    VelocityInstance &operator=(const VelocityInstance &orig);
    VelocityInstance(const VelocityInstance &other);

    void configureEstimator();
    void publishInternal(const VelocityEstimator::Result &result);

    VelocityEstimator estimator_;
    uint64_t last_source_time_;

    friend class MachineInstanceFactory;
};

#endif
