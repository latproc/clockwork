#ifndef __MACHINESHADOWINSTANCE_H__
#define __MACHINESHADOWINSTANCE_H__

#include "MachineInstance.h"

class MachineShadowInstance : public MachineInstance {
  protected:
    MachineShadowInstance(InstanceType instance_type = MACHINE_INSTANCE);
    MachineShadowInstance(CStringHolder name, const char *type,
                          InstanceType instance_type = MACHINE_INSTANCE);

  private:
    MachineShadowInstance &operator=(const MachineShadowInstance &orig);
    MachineShadowInstance(const MachineShadowInstance &other);
    MachineShadowInstance *settings;

  public:
    MachineShadowInstance();
    ~MachineShadowInstance();
    virtual void idle();
    virtual bool isShadow() { return true; }

    virtual Action::Status setState(const State &new_state,
                                    uint64_t authority = 0,
                                    bool resume = false);
    virtual Action::Status setState(const char *new_state,
                                    uint64_t authority = 0,
                                    bool resume = false);

    friend class MachineInstanceFactory;
};

#endif
